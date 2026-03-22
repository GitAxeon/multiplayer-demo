#pragma once

#include <variant>
#include <asio/ip/udp.hpp>

#include "Listener.hpp"
#include "Connection.hpp"
#include "Buffer.hpp"
#include "Error.hpp"

namespace Networking
{

namespace Event
{

struct ListenerStarted
{
    ListenerHandle handle;
};

struct ListenerClosed
{
    ListenerHandle handle;
    ListenerError listenerError;
    TransportError transportError{TransportError::None}; 
};

struct IncomingConnectionPending
{
    ListenerHandle listener;
    ConnectionHandle handle;
    asio::ip::udp::endpoint remoteEndpoint;
};

struct IncomingConnectionAccepted
{
    ListenerHandle listener;
    ConnectionHandle connection;
    asio::ip::udp::endpoint remoteEndpoint;
};

struct OutgoingConnectionEstablished
{
    ConnectionHandle handle;
    asio::ip::udp::endpoint remoteEndpoint;
};

struct ConnectionClosed
{
    ConnectionHandle handle;
    ConnectionError connectionError;
    TransportError transportError;
};

struct DataReceived
{
    ConnectionHandle handle;
    ByteVector data;
};

}

using InternalEvent = std::variant
<
    Event::ListenerStarted,
    Event::ListenerClosed,

    Event::IncomingConnectionPending,
    Event::IncomingConnectionAccepted,

    Event::OutgoingConnectionEstablished,
    
    Event::DataReceived,
    Event::ConnectionClosed
>;

}