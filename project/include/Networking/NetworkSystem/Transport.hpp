#pragma once 

#include <cstddef>
#include <span>

#include <asio.hpp>

#include "AsyncSocket.hpp"

namespace Networking
{

class ITransport
{
public:
    virtual ~ITransport() = default;
    virtual void Send(std::span<std::byte> data, const asio::ip::udp::endpoint& endpoint) = 0;
};

class ClientTransport : public ITransport
{
public:
    ClientTransport(asio::io_context& context)
        : m_Socket(context) {}

    void Send(std::span<std::byte> data, const asio::ip::udp::endpoint& endpoint) override
    {
        m_Socket.Send(data, endpoint);
    }

private:
    AsyncSocket m_Socket;
};

class ServerTransport : public ITransport
{
public:
    ServerTransport(AsyncSocket& socket) 
        : m_Socket(socket) {}
    
    void Send(std::span<std::byte> data, const asio::ip::udp::endpoint& endpoint) override
    {
        m_Socket.Send(data, endpoint);
    }

private:
    AsyncSocket& m_Socket;
};


}