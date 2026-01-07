#pragma once

#include <cstdint>
#include <memory>
#include <bit>
#include <algorithm>
#include <string>
#include <span>

namespace Networking
{

// On little endian platforms std::copy, big endian platforms std::reverse_copy
inline void XCopy(const std::byte* source, std::byte* destination, std::size_t count)
{
    if constexpr (std::endian::native == std::endian::little)
    {
        std::copy(source, source + count, destination);
    }
    else if constexpr(std::endian::native == std::endian::big)
    {
        std::reverse_copy(source, source + count, destination);
    }
    else
    {
        static_assert
        (
            // native must be either little or big
            (std::endian::native == std::endian::little || std::endian::native == std::endian::big), 
            "Unsupported mixed endian platform"
        );
    }
}

// template<typename T>
// concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template<typename T> struct network_trivial : std::false_type {};

template<> struct network_trivial<std::byte> : std::true_type {};

// Enable if needed - Use for text only, no numerical values
// template<> struct NetworkTrivial<char> : std::true_type {};

// Will use std::uint8_t for storage since the data isn't bit packed
template<> struct network_trivial<bool> : std::true_type {};

// Note: Possibly need to check for validity
template<> struct network_trivial<float> : std::true_type {};
template<> struct network_trivial<double> : std::true_type {};

template<> struct network_trivial<std::uint8_t> : std::true_type {};
template<> struct network_trivial<std::uint16_t> : std::true_type {};
template<> struct network_trivial<std::uint32_t> : std::true_type {};
template<> struct network_trivial<std::uint64_t> : std::true_type {};

template<> struct network_trivial<std::int8_t> : std::true_type {};
template<> struct network_trivial<std::int16_t> : std::true_type {};
template<> struct network_trivial<std::int32_t> : std::true_type {};
template<> struct network_trivial<std::int64_t> : std::true_type {};

template<typename T> constexpr bool network_trivial_v = network_trivial<T>::value;
template<typename T> concept NetworkTrivialType = network_trivial_v<T>;

class Buffer
{
public:
    explicit Buffer(std::size_t size)
        : m_Data(std::make_unique<std::byte[]>(size)), m_Capacity(size), m_Size(0)
    {}

    template<typename... Args>
    static inline std::shared_ptr<Buffer> CreateShared(Args&&... args)
    {
        return std::make_shared<Buffer>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    static inline std::unique_ptr<Buffer> CreateUnique(Args&&... args)
    {
        return std::make_unique<Buffer>(std::forward<Args>(args)...);
    }

    std::size_t Capacity() const { return m_Capacity; }
    std::size_t Size() const { return m_Size; }
    void Clear() { m_Size = 0; }
    bool SetSize(std::size_t size)
    {
        if(size > m_Capacity) { return false; }

        m_Size = size;
        return true;
    }

    std::span<const std::byte> View() const
    {
        return std::span<const std::byte>(m_Data.get(), m_Size);
    }

    std::byte* Data() { return m_Data.get(); }
    const std::byte* Data() const { return m_Data.get(); }
    
    template<typename T>
    bool Read(T& destination, std::size_t position) const
    {
        if(position + sizeof(T) > m_Size) { return false; }

        XCopy(m_Data.get() + position, reinterpret_cast<std::byte*>(&destination), sizeof(T));
        return true;
    }

    template<typename T>
    bool ReadN(T* destination, std::size_t position, std::size_t count) const
    {
        const auto byteCount = sizeof(T) * count;

        if((position + byteCount) > m_Size) { return false; }

        XCopy(m_Data.get() + position, reinterpret_cast<std::byte*>(destination), byteCount);
        return true;
    }

    template<typename T>
    bool Write(const T& value, std::size_t position)
    {
        const auto end = position + sizeof(T);

        if(end > m_Capacity) { return false; }

        XCopy(reinterpret_cast<const std::byte*>(&value), m_Data.get() + position, sizeof(T));

        if(end > m_Size) { m_Size = end; }

        return true;
    }

    template<typename T>
    bool WriteN(const T* source, std::size_t position, std::size_t count)
    {
        const auto byteCount = sizeof(T) * count;
        const auto end = position + byteCount;

        if(end > m_Capacity) { return false; }

        XCopy(reinterpret_cast<const std::byte*>(source), m_Data.get() + position, byteCount);

        if(end > m_Size) { m_Size = end; }

        return true;
    }
    

private:
    std::unique_ptr<std::byte[]> m_Data;
    std::size_t m_Capacity;

    // Points to the position of highest write
    std::size_t m_Size;
};

class StreamBase
{
public:
    std::size_t GetStreamPosition() const { return m_Position; }
    bool SetStreamPosition(std::size_t position)
    { 
        m_Position = position;
        return true;
    }

    bool Ok() const { return m_Ok; }

protected:
    std::size_t m_Position{0};
    bool m_Ok{true};
};

class StreamReader : public StreamBase
{
public:
    explicit StreamReader(Buffer& buffer) : m_Buffer(buffer) { }
    
    template<typename T>
    bool Read(T& destination)
    {
        if(!m_Ok) 
            return false;
        
        bool result = true;

        if constexpr (NetworkTrivialType<T>)
            result = ReadTrivial(destination);
        else 
            result = ReadNonTrivial(destination);
        
        if(!result)
        {
            m_Ok = false;
        }

        return result;
    }

private:
    template<typename T>
    bool ReadTrivial(T& destination)
    {
        if(!m_Buffer.Read<T>(destination, m_Position)) { return false; }
        
        m_Position += sizeof(T);
        return true;
    }

    template<>
    bool ReadTrivial(bool& destination)
    {
        std::uint8_t value = 0;

        if(!m_Buffer.Read(value, m_Position)) { return false; }

        // Note: Technically after read 'value' could be anything 
        destination = value ? true : false;

        m_Position += sizeof(std::uint8_t);
        return true;
    }

    template<typename T>
    bool ReadNonTrivial(T& destination)
    {
        return Deserialize(*this, destination);
    }

private:
    bool ReadNonTrivial(std::string& destination)
    {
        uint32_t length{0};
        
        if(!Read(length)) { return false; }
        
        if(length == 0) { return true; }
        
        destination.resize(length);

        bool result = m_Buffer.ReadN(destination.data(), m_Position, length);

        if(result) { m_Position += static_cast<std::size_t>(length); }

        return result;
    }

private:
    Buffer& m_Buffer;
};

class StreamWriter : public StreamBase
{
public:
    explicit StreamWriter(Buffer& buffer) : m_Buffer(buffer) { }
    
    template<typename T>
    bool Write(const T& source)
    {
        if(!m_Ok)
            return false;
        
        bool result = true;
        if constexpr(NetworkTrivialType<T>)
            result = WriteTrivial(source);
        else
            result = WriteNonTrivial(source);

        if(!result)
        {
            m_Ok = false;
        }

        return result;
    }

private:
    template<typename T>
    bool WriteTrivial(const T& source)
    {
        if(m_Buffer.Write<T>(source, m_Position))
        {
            m_Position += sizeof(T);
            return true;
        }

        return false;
    }

    // Boolean written as a std::uint8_t
    template<>
    bool WriteTrivial(const bool& source)
    {
        std::uint8_t value = source ? 1 : 0;

        if(m_Buffer.Write(value, m_Position))
        {
            m_Position += sizeof(std::uint8_t);
            return true;
        }

        return false;
    }

    template<typename T>
    bool WriteNonTrivial(const T& source)
    {
        return Serialize(*this, source);
    }

    // std::string 
    bool WriteNonTrivial(const std::string& source)
    {
        // Reading the size as a fixed sized integer because it will be written to the buffer
        const std::uint32_t length = static_cast<uint32_t>(source.size());

        if(!Write(length)) { return false; }
        
        if(length == 0) { return true; }

        bool result = m_Buffer.WriteN(source.data(), m_Position, source.length());

        if(result) { m_Position += static_cast<std::size_t>(length); }

        return result;
    }
    
    // Write the bytes as is so the source is expected to contain serialized data, it won't be able to be read back as a Buffer
    bool WriteNonTrivial(const Buffer& source)
    {
        const uint32_t length = static_cast<uint32_t>(source.Size());
        
        if(length == 0) { return true; }
        
        bool result = m_Buffer.WriteN(source.Data(), m_Position, source.Size());

        if(result) { m_Position += static_cast<std::size_t>(length); }

        return result;
    }
    
private:
    Buffer& m_Buffer;
};

}