#pragma once

#include "AsyncSocket.hpp"
#include "Types.hpp"

namespace Networking
{

enum class ConnectionState
{
    Connecting,
    Connected,
    Closing,
    Closed
};

struct Connection2
{
    Connection2
    (
        ConnectionHandle handle, 
        SocketHandle socket,
        asio::ip::udp::endpoint remote,
        ListenerHandle listener = ListenerHandle::Invalid
    ) : m_Handle(handle), m_Socket(socket), m_Listener(listener), m_Remote(remote)
    {}

public:
    ConnectionHandle m_Handle;
    SocketHandle m_Socket;
    ListenerHandle m_Listener; /* Contains a valid handle if connection is inbound */
    asio::ip::udp::endpoint m_Remote;

    ConnectionState m_State{ConnectionState::Closed};
};

}