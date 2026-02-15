#pragma once

#include <memory>

#include "AsyncSocket.hpp"
#include "Transport.hpp"
#include "Handle.hpp"

namespace Networking
{

using ConnectionHandle = asd::Handle<struct ConnectionTag>;

struct Connection2
{
    Connection2(ConnectionHandle handle, std::unique_ptr<ITransport> transport, asio::ip::udp::endpoint remote)
        : handle(handle), m_Transport(std::move(transport)) {}
    
    void Send(std::span<const std::byte> data)
    {
        m_Transport->Send(data, m_Remote);
    }

    void Close()
    {
        m_Transport->Close();
    }

public:
    ConnectionHandle handle;
    std::unique_ptr<ITransport> m_Transport{nullptr};
    asio::ip::udp::endpoint m_Remote;
};

}