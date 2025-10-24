#pragma once

#include "NetworkBuffer.hpp"
#include "Message.hpp"

namespace Networking
{
    template<typename T>
    void Serialize(const T&, Buffer&)
    { 
        static_assert(sizeof(T) == 0, "Serialize<T> not implemented");
    }

    template<typename T>
    void Deserialize(T&, Buffer&)
    {
        static_assert(sizeof(T) == 0, "Deserialize<T> not implemented");
    }

    template<>
    inline void Serialize(const Header& header, Buffer& buffer)
    {
        buffer.Write(header.protocol);
        buffer.Write(header.sequence);
        buffer.Write(header.timestamp);
        buffer.Write(header.flags);
        buffer.Write(header.clientId);
        buffer.Write(header.messageType);
    }

    template<>
    inline void Deserialize(Header& header, Buffer& buffer)
    {
        buffer.Read(header.protocol);
        buffer.Read(header.sequence);
        buffer.Read(header.timestamp);
        buffer.Read(header.flags);
        buffer.Read(header.clientId);
        buffer.Read(header.messageType);
    }
}