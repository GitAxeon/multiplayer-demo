#pragma once

#include <memory>

#include "AsyncSocket.hpp"
#include "Handle.hpp"

namespace Networking
{

// using ListenerHandle = std::uint32_t;
// inline constexpr ListenerHandle InvalidListenerHandle = 0;

using ListenerHandle = asd::Handle<struct ListenerTag>;

struct Listener2
{
    Listener2(ListenerHandle handle, asio::io_context& context)
        : handle(handle), socket(std::make_unique<AsyncSocket>(context)) {}

    void HandleDatagram(const IncomingDatagram& datagram)
    {
        
    }
    
    ListenerHandle handle;
    std::unique_ptr<AsyncSocket> socket{nullptr};
};

}