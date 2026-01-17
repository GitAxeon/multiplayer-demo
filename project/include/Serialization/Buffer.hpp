#pragma once

#include <memory>
#include <bit>
#include <algorithm>
#include <span>

namespace Networking
{

// Forward declaration
// On little endian platforms std::copy, big endian platforms std::reverse_copy
inline void XCopy(const std::byte* source, std::byte* destination, std::size_t count);

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

}