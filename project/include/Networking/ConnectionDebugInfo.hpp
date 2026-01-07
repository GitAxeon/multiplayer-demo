#pragma once

#include <cstdint>
#include <asio.hpp>

namespace Networking
{

struct ConnectionDebugInfo
{
    asio::ip::udp::endpoint localEndpoint;
    asio::ip::udp::endpoint remoteEndpoint;

    uint32_t localSequence;
    uint32_t remoteSequence;
    uint32_t acknowledgeBits;

    std::chrono::milliseconds timeSinceLastReceive;
    std::chrono::milliseconds timeSinceLastSend;
};

}