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
    virtual void Send(std::span<const std::byte> data, const asio::ip::udp::endpoint& endpoint) = 0;
    virtual void Close() = 0;
};

class ClientSideTransport final : public ITransport
{
public:
    explicit ClientSideTransport(std::unique_ptr<AsyncSocket> socket)
        : m_Socket(std::move(socket)) {}

    void Send(std::span<const std::byte> data, const asio::ip::udp::endpoint& endpoint) override
    {
        m_Socket->Send(data, endpoint);
    }

    void Close()
    {
        m_Socket->Close();
    }

private:
    std::unique_ptr<AsyncSocket> m_Socket;
};

class ServerSideTransport final : public ITransport
{
public:
    explicit ServerSideTransport(AsyncSocket& socket) 
        : m_Socket(socket) {}
    
    void Send(std::span<const std::byte> data, const asio::ip::udp::endpoint& endpoint) override
    {
        m_Socket.Send(data, endpoint);
    }

    void Close() { }

private:
    AsyncSocket& m_Socket;
};


}