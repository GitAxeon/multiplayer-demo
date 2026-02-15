#pragma once

#include <memory>
#include <optional>

#include "AsyncSocket.hpp"
#include "Handle.hpp"
#include "Connection.hpp"

namespace Networking
{

using ListenerHandle = asd::Handle<struct ListenerTag>;

struct Listener2
{
    Listener2(ListenerHandle handle, asio::io_context& context)
        : handle(handle), socket(std::make_unique<AsyncSocket>(context)) {}
    
    Listener2(ListenerHandle handle, std::unique_ptr<AsyncSocket> socket)
        : handle(handle), socket(std::move(socket)) {}

    void Close()
    {
        socket->Close();
    }

    std::optional<Connection> HandleDatagram(const IncomingDatagram& datagram)
    {
        
    }

public:
    ListenerHandle handle;
    std::unique_ptr<AsyncSocket> socket{nullptr};
};

}