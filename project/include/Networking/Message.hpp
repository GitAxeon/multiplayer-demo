#pragma once

#include <chrono>
#include <cstdint>

#include "Buffer.hpp"

namespace Networking
{

inline uint64_t TimeAsMilliseconds()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/*

Client->Server: CONNECTION_REQUEST
Server->Client: CHALLENGE 
Client->Server: CHALLENGE_RESPONSE
Server->Client: CONNECTION_ACCEPTED 

*/

enum class MessageType : uint8_t
{
    CONNECTION_REQUEST,
    CHALLENGE,
    CHALLENGE_RESPONSE,
    CONNECTION_ACCEPTED,
    HEARTBEAT,
    MESSAGE,
    ACKNOWLEDGE,
    DISCONNECT
};

inline static uint32_t sProtocol {88173283U};

using UDPFlag = std::uint8_t;

constexpr UDPFlag UDP_Unreliable = 1 << 0;
constexpr UDPFlag UDP_Reliable = 1 << 1;
constexpr UDPFlag UDP_OrderedReliable = 1 << 2;

enum class Reliable : std::uint8_t
{
    Unreliable,
    Reliable
};

using ClientId = std::uint8_t;

struct Header
{
    std::uint32_t protocol{sProtocol};
    std::uint32_t sequence{0};
    std::uint32_t acknowledged{0};
    std::uint32_t acknowledgeBits{0};
    std::uint64_t timestamp{0};
    UDPFlag flags{0};
    ClientId clientId{0};
    MessageType messageType;
};

bool Serialize(StreamWriter& serializer, const Header& header)
{
    serializer.Write(header.protocol);
    serializer.Write(header.sequence);
    serializer.Write(header.acknowledged);
    serializer.Write(header.acknowledgeBits);
    serializer.Write(header.timestamp);
    serializer.Write(header.flags);
    serializer.Write(header.clientId);
    serializer.Write(static_cast<std::uint8_t>(header.messageType));

    return serializer.Ok();
}

bool Deserialize(StreamReader& deserializer, Header& header)
{
    deserializer.Read(header.protocol);
    deserializer.Read(header.sequence);
    deserializer.Read(header.acknowledged);
    deserializer.Read(header.acknowledgeBits);
    deserializer.Read(header.timestamp);
    deserializer.Read(header.flags);
    deserializer.Read(header.clientId);

    std::uint8_t messageType = 0;
    deserializer.Read(messageType);
    header.messageType = static_cast<MessageType>(messageType);

    return deserializer.Ok();
}

}