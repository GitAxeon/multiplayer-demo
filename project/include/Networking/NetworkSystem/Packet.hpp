#pragma once

#include "Buffer.hpp"
#include "Serialization/Serialization.hpp"

namespace Networking
{

inline static uint32_t sProtocol2 {88173283U};

struct Packet
{
    struct Header
    {
        std::uint32_t protocol{0};
        std::uint32_t connectionId{0};
        std::uint32_t idGeneration{0};
        std::uint32_t sequence{0};
        std::uint32_t acknowledged{0};
        std::uint32_t acknowledgeBits{0};
    };

    Header header;
    ByteVector data;
};

bool Deserialize(Serialization::StreamReader& reader, Packet::Header& header)
{
    reader.Read(header.protocol);
    reader.Read(header.connectionId);
    reader.Read(header.idGeneration);
    reader.Read(header.sequence);
    reader.Read(header.acknowledged);
    reader.Read(header.acknowledgeBits);

    return reader.Ok();
}

bool Serialize(Serialization::StreamWriter& writer, const Packet::Header& header)
{
    writer.Write(header.protocol);
    writer.Write(header.connectionId);
    writer.Write(header.idGeneration);
    writer.Write(header.sequence);
    writer.Write(header.acknowledged);
    writer.Write(header.acknowledgeBits);

    return writer.Ok();
}

}