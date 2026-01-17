#pragma once

#include "Buffer.hpp"
#include "StreamBase.hpp"
#include "NetworkTrivial.hpp"

namespace Networking
{

class StreamReader;

template<typename T>
bool Deserialize(StreamReader& reader, T& destination)
{
    static_assert("Please define bool Serialize(StreamReader&, T&)");
}

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

}