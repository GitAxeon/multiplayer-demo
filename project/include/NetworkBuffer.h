#pragma once

#include <memory>
#include <bit>
#include <concepts>
#include <algorithm>
#include <string>
#include <stdexcept>

namespace Networking
{

void CopyToNetworkOrder(const std::byte* source, std::byte* destination, std::size_t count);
void CopyToNativeOrder(const std::byte* source, std::byte* destination, std::size_t count);

struct Buffer
{
public:
    explicit Buffer(std::size_t size);

    ~Buffer() = default;
    
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&&) = default;
    Buffer& operator=(Buffer&&) = default;

    void Clear();
    void Reset();

    void Read(std::integral auto& destination);
    void Write(const std::integral auto& value);

    void Read(std::floating_point auto& destination);
    void Write(const std::floating_point auto& value);

    void Read(std::string& destination);
    void Write(const std::string& value);

    std::byte* Data() const;
    std::size_t Size() const;
    std::size_t Capacity() const;

private:
    template<typename T>
    void ReadImpl(T& destination)
    {
        if(m_Position + sizeof(T) > m_Capacity)
            throw std::runtime_error("Networking::Buffer overflow on read");

        CopyToNativeOrder(m_Data.get() + m_Position, reinterpret_cast<std::byte*>(&destination), sizeof(T));
        m_Position += sizeof(T);
    }

    template<typename T>
    void WriteImpl(const T& value)
    {
        if(m_Position + sizeof(T) > m_Capacity)
            throw std::runtime_error("Networking::Buffer overflow on write");

        CopyToNetworkOrder(reinterpret_cast<const std::byte*>(&value), m_Data.get() + m_Position, sizeof(T));
        m_Position += sizeof(T);
    }

private:
    std::unique_ptr<std::byte[]> m_Data;
    std::size_t m_Capacity {0};
    std::size_t m_Position {0};
};

// Free functions
void CopyToNetworkOrder(const std::byte* source, std::byte* destination, std::size_t count)
{
    if constexpr (std::endian::native == std::endian::little)
    {
        std::reverse_copy(source, source + count, destination);
    }
    else if constexpr (std::endian::native == std::endian::big)
    {
        std::copy(source, source + count, destination);
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

void CopyToNativeOrder(const std::byte* source, std::byte* destination, std::size_t count)
{
    CopyToNetworkOrder(source, destination, count);
}

// Buffer
Buffer::Buffer(std::size_t size)
    : m_Data(std::make_unique<std::byte[]>(size)), m_Capacity(size)
{
    Clear();
}

void Buffer::Clear()
{
    if(m_Data)
    {
        std::fill(m_Data.get(), m_Data.get() + m_Capacity, std::byte{0});
    }

    m_Position = 0;
}

void Buffer::Read(std::integral auto& destination)
{
    ReadImpl(destination);
}

void Buffer::Write(const std::integral auto& value)
{
    WriteImpl(value);
}

void Buffer::Read(std::floating_point auto& destination)
{
    ReadImpl(destination);
}

void Buffer::Write(const std::floating_point auto& value)
{
    WriteImpl(value);
}

void Buffer::Read(std::string& destination)
{
    std::size_t length = 0;
    Read(length);
        
    if(m_Position + length > m_Capacity)
        throw std::runtime_error("Networking::Buffer overflow on read");
    
    destination.resize(length);
    std::memcpy(destination.data(), m_Data.get() + m_Position, length);
    m_Position += length;
}

void Buffer::Write(const std::string& value)
{
    std::size_t length = value.size();
    Write(length);
    
    if(m_Position + length > m_Capacity)
        throw std::runtime_error("Networking::Buffer overflow on write");
    
    std::memcpy(m_Data.get() + m_Position, value.data(), length);
    m_Position += length;
}

std::byte* Buffer::Data() const
{
    return m_Data.get();
}

std::size_t Buffer::Size() const
{
    return m_Position;
}

std::size_t Buffer::Capacity() const
{
    return m_Capacity;
}


}