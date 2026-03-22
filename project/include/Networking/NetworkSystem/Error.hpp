#pragma once

#include <asio/error_code.hpp>

namespace Networking
{

enum class TransportError
{
    None,
    Unknown,
    AccessDenied,
    AddressInUse,
    AddressNotAvailable,

    MessageTooLong,
    
    NetworkDown,
    NetworkUnreachable,
    HostUnreachable
};

enum class ListenerError
{
    UserRequestedClose,
    TransportError
};


enum class ConnectionError
{
    UserRequestedClose,
    RemoteClosed,
    LocalClosed,
    Timeout,
    ProtocolError,
    Rejected
};

TransportError TranslateError(asio::error_code const& error)
{
    if(!error)
        return TransportError::None;

    switch(error.value())
    {
        using namespace asio::error;

        case access_denied: return TransportError::AccessDenied;
        case address_in_use: return TransportError::AddressInUse;
        case fault: return TransportError::AddressNotAvailable;
        case message_size: return TransportError::MessageTooLong;
        case network_down: return TransportError::NetworkDown;
        case network_unreachable: return TransportError::NetworkUnreachable;
        case host_unreachable: return TransportError::HostUnreachable;
        default: return TransportError::Unknown;
    }
}

}