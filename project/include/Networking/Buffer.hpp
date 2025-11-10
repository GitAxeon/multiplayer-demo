#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <bit>
#include <algorithm>
#include <print>

namespace Networking
{

// On little endian platforms std::copy, big endian platforms std::reverse_copy
void XCopy(const std::byte* source, std::byte* destination, std::size_t count)
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

template<typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

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
        if(size > m_Capacity)
        {
            return false;
        }

        m_Size = size;
        return true;
    }

    std::byte* Data() { return m_Data.get(); }
    const std::byte* Data() const { return m_Data.get(); }
    
    template<TriviallyCopyable T>
    bool Read(T& destination, std::size_t position) const
    {
        if(position + sizeof(T) > m_Size)
        {
            return false;
        }

        XCopy(m_Data.get() + position, reinterpret_cast<std::byte*>(&destination), sizeof(T));
        return true;
    }

    template<TriviallyCopyable T>
    bool ReadN(T* destination, std::size_t position, std::size_t count) const
    {
        const auto byteCount = sizeof(T) * count;

        if((position + byteCount) > m_Size)
        {
            return false;
        }

        XCopy(m_Data.get() + position, reinterpret_cast<std::byte*>(destination), byteCount);
        return true;
    }

    template<TriviallyCopyable T>
    bool Write(const T& value, std::size_t position)
    {
        const auto end = position + sizeof(T);

        if(end > m_Capacity)
        {
            return false;
        }

        XCopy(reinterpret_cast<const std::byte*>(&value), m_Data.get() + position, sizeof(T));

        if(end > m_Size)
            m_Size = end;

        return true;
    }

    template<TriviallyCopyable T>
    bool WriteN(const T* source, std::size_t position, std::size_t count)
    {
        const auto byteCount = sizeof(T) * count;
        const auto end = position + byteCount;

        if(end > m_Capacity)
        {
            return false;
        }

        XCopy(reinterpret_cast<const std::byte*>(source), m_Data.get() + position, byteCount);

        if(end > m_Size)
            m_Size = end;

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

protected:
    std::size_t m_Position{0};
};

class StreamReader : public StreamBase
{
public:
    explicit StreamReader(Buffer& buffer) : m_Buffer(buffer) { }
    
    template<typename T>
    bool Read(T& destination)
    {
        if constexpr (TriviallyCopyable<T>)
            return ReadTrivial(destination);
        else 
            return ReadNonTrivial(destination);
    }

private:
    template<typename T>
    bool ReadTrivial(T& destination)
    {
        if(!m_Buffer.Read<T>(destination, m_Position))
        {
            return false;
        }
        
        m_Position += sizeof(T);
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
        
        if(!Read(length))
            return false;
        
        if(length == 0)
            return true;
        
        destination.resize(length);

        bool result = m_Buffer.ReadN(destination.data(), m_Position, length);

        if(result)
            m_Position += static_cast<std::size_t>(length);

        return result;
    }

    bool ReadNonTrivial(Buffer& destination)
    {
        uint32_t length{0};

        if(!Read(length))
            return false;
        
        if(length == 0)
            return true;
        
        if(destination.Capacity() < length)
            return false;
        
        bool result = m_Buffer.ReadN(destination.Data(), m_Position, length);

        if(result)
            m_Position += static_cast<std::size_t>(length);
        
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
        if constexpr(TriviallyCopyable<T>)
            return WriteTrivial(source);
        else
            return WriteNonTrivial(source);
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

    template<typename T>
    bool WriteNonTrivial(const T& source)
    {
        return Serialize(*this, source);
    }

    bool WriteNonTrivial(const std::string& source)
    {   
        const uint32_t length = static_cast<uint32_t>(source.size());

        if(!Write(length))
            return false;
        
        if(length == 0)
            return true;

        bool result = m_Buffer.WriteN(source.data(), m_Position, source.length());

        if(result)
            m_Position += static_cast<std::size_t>(length);

        return result;
    }

    bool WriteNonTrivial(const Buffer& source)
    {
        const uint32_t length = static_cast<uint32_t>(source.Size());

        if(!Write(length))
            return false;
        
        if(length == 0)
            return true;
        
        bool result = m_Buffer.WriteN(source.Data(), m_Position, source.Size());

        if(result)
            m_Position += static_cast<std::size_t>(length);

        return result;
    }
    
private:
    Buffer& m_Buffer;
};

}