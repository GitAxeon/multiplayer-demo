#pragma once

#include <memory>
#include <optional>
#include <chrono>
#include <unordered_map>

#include "AsyncSocket.hpp"
#include "Handle.hpp"
#include "Connection.hpp"
#include "Types.hpp"

namespace Networking
{

enum class ListenerState
{
    Open,
    Closing,
    Closed
};

struct NewConnectionInfo
{
    std::uint32_t sequence{0};
    std::uint32_t remoteSequence{0};
};

struct PendingConnection
{
    asio::ip::udp::endpoint endpoint;
    std::uint32_t challenge{0};
    std::uint32_t sequence{0};
    std::uint32_t remoteSequence{0};
    std::chrono::steady_clock::time_point lastReceive;
};

struct Listener2
{
    Listener2(ListenerHandle handle)
        : m_Handle(handle) {}
    
    Listener2(ListenerHandle handle, SocketHandle socket)
        : m_Handle(handle), m_Socket(socket) {}

    std::optional<NewConnectionInfo> OnReceiveData(IncomingDatagram const& datagram)
    {
        NewConnectionInfo info
        {
            .sequence = 0,
            .remoteSequence = 0
        };

        return info;
    }

public:
    ListenerHandle m_Handle;
    SocketHandle m_Socket;

    ListenerState m_State{ListenerState::Closed};

};

}