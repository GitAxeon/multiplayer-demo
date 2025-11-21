#pragma once

#include <chrono>
#include <cstdint>

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
    MESSAGE,
    ACKNOWLEDGE,
    DISCONNECT
};

inline static uint32_t sProtocol {88173283U};

using UDPFlag = uint8_t;

constexpr UDPFlag UDP_Unreliable = 1 << 0;
constexpr UDPFlag UDP_Reliable = 1 << 1;
constexpr UDPFlag UDP_OrderedReliable = 1 << 2;

using ClientId = uint8_t;

struct Header
{
    uint32_t protocol{sProtocol};
    uint32_t sequence{0};
    uint32_t acknowledged{0};
    uint32_t acknowledgeBits{0};
    uint64_t timestamp{0};
    UDPFlag flags{0};
    ClientId clientId{0};
    MessageType messageType;
};

}