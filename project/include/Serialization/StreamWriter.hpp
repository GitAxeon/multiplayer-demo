#pragma once

#include "Buffer.hpp"
#include "Streambase.hpp"
#include "NetworkTrivial.hpp"

namespace Networking
{
class StreamWriter;

template<typename T>
bool Serialize(StreamWriter& writer, const T& source)
{
    static_assert(false, "Please define bool Serialize(StreamWriter&, const T&)");
}

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

    bool WriteNonTrivial(std::span<const std::byte> span)
    {
        if(span.empty()) { return true; }

        bool result = m_Buffer.WriteN(span.data(), m_Position, span.size());

        if(result) { m_Position += span.size(); }

        return result;
    }
    
private:
    Buffer& m_Buffer;
}; 

}