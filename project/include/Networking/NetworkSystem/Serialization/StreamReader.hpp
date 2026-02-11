#pragma once

#include <concepts>
#include <span>
#include <ranges>

#include "StreamBase.hpp"
#include "NetworkTrivial.hpp"
#include "Endian.hpp"

namespace Serialization
{

class StreamReader : public StreamBase
{
public:
    StreamReader(std::span<const std::byte> buffer)
        : m_Buffer(buffer) {}
    
    // For integers, float/double and std::byte
    template<NetworkTrivial T>
    bool Read(T& value)
    {
        if(!m_Ok)
            return false;
         
        const auto destination = std::as_writable_bytes(std::span{&value, 1});
        const bool result = ReadBytes(destination);

        if(!result)
        {
            m_Ok = false;
            return false;
        }

        if constexpr (std::endian::native != Networking::NetworkEndian)
        {
            value = SwapEndian(value);
        }

        return true;
    }

    // bool is stored as one byte
    bool Read(bool& value)
    {
        std::uint8_t v = 0;
        if(Read(v))
        {
            value = (v != 0);
            return true;
        }

        return false;
    }

    // For non trivial types (basically anything other than a scalar)
    template<NetworkSerializable T>
    bool Read(T& value)
    {
        if(!m_Ok)
            return false;

        const bool result = Deserialize(*this, value);

        if(!result)
            m_Ok = false;

        return m_Ok;
    }

    // Reads bytes as is
    bool ReadBytes(std::span<std::byte> destination)
    {
        if(!m_Ok)
            return false;

        if(m_Position + destination.size() > m_Buffer.size())
        {
            m_Ok = false;
            return m_Ok;
        }

        std::ranges::copy
        (
            m_Buffer.subspan(m_Position, destination.size()),
            destination.begin()
        );

        m_Position += destination.size();

        return true;
    }
private:
    std::span<const std::byte> m_Buffer;
};

}