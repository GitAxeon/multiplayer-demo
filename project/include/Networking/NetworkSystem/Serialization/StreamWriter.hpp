#pragma once

#include <concepts>
#include <span>
#include <ranges>
#include <string_view>

#include "StreamBase.hpp"
#include "NetworkTrivial.hpp"
#include "Endian.hpp"

namespace Serialization
{

class StreamWriter : public StreamBase
{
public:
    explicit StreamWriter(std::span<std::byte> buffer)
        : m_Buffer(buffer) {}

    template<typename T>
    bool Write(T*) = delete;

    // For integers, float/double and std::byte
    template<NetworkTrivial T>
    bool Write(T value)
    {
        if(!m_Ok)
            return false;

        if constexpr (std::endian::native != Networking::NetworkEndian)
        {
            value = SwapEndian(value);
        }

        // value is local so taking ref is safe
        const auto bytes = std::as_bytes(std::span{&value, 1});
        
        // Does second second check for m_Ok
        const bool result = WriteBytes(bytes);
        
        if(!result)
            m_Ok = false;

        return m_Ok;
    }

    // For non trivial types (basically anything other than a scalar)
    template<NetworkSerializable T>
    bool Write(T const& value)
    {
        if(!m_Ok)
            return false;

        const bool result = Serialize(*this, value);

        if(!result)
            m_Ok = false;

        return m_Ok;
    }

    // Copies bytes to buffer as is
    bool WriteBytes(std::span<const std::byte> source)
    {
        if(!m_Ok)
            return false;
            
        if(m_Position + source.size() > m_Buffer.size())
        {
            m_Ok = false;
            return false;
        }

        std::ranges::copy(source, m_Buffer.begin() + m_Position);
        m_Position += source.size();

        return true;
    }

    // bool is stored as one byte
    bool Write(bool value)
    {
        std::uint8_t v = value ? 1 : 0;

        return Write(v);
    }

    bool Write(std::string_view value)
    {
        if(!m_Ok)
            return false;

        Write(static_cast<std::uint64_t>(value.size()));
        WriteBytes(std::as_bytes(std::span{value}));

        return true;
    }
    
private:
    std::span<std::byte> m_Buffer;
};

}