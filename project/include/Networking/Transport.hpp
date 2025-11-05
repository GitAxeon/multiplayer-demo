#pragma once

#include <functional>
#include <print>

#include <asio.hpp>

#include "AsioFormat.hpp"
#include "NetworkBuffer.hpp"

namespace Networking
{

struct Transport
{
    using ReceiveCallback = std::function<void(const asio::ip::udp::endpoint&, Buffer&)>; 

    asio::ip::udp::socket m_Socket;
    ReceiveCallback m_ReceiveCallback;

    // Used when receiving data
    Buffer m_ReceiveBuffer{512};
    asio::ip::udp::endpoint m_RemoteEndpoint;
    
    Transport(asio::io_context& context)
        : m_Socket(context)
    {}

    Transport(asio::io_context& context, asio::ip::udp::endpoint endpoint)
        : m_Socket(context, endpoint)
    {}

    // No-copyable
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // Movable (thought it was spelled moveable)
    Transport(Transport&&) = default;
    Transport& operator=(Transport&&) = default;

    asio::ip::udp::endpoint LocalEndpoint()
    {
        return m_Socket.local_endpoint();
    }

    void SetReceiveCallback(ReceiveCallback callback)
    {
        m_ReceiveCallback = callback;
    }

    bool Bind(const asio::ip::udp::endpoint& endpoint)
    {
        if(m_Socket.is_open())
        {
            asio::error_code closeError;
            m_Socket.close(closeError);

            if(closeError)
            {
                std::println("Failed to close socket cleanly: {}", closeError.message());
            }
        }

        asio::error_code openError;
        m_Socket.open(endpoint.protocol(), openError);
        
        if(openError)
        {
            std::println("Failed to open socket: {}", openError.message());
            return false;
        }

        asio::error_code bindError;
        m_Socket.bind(endpoint, bindError);

        if(bindError)
        {
            std::println("Failed to bind socket: {}", bindError.message());
            return false;
        }

        return true;
    }

    void Send(std::shared_ptr<Buffer> buffer, const asio::ip::udp::endpoint& endpoint)
    {
        m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), endpoint, [buffer, endpoint](std::error_code ec, std::size_t length)
        {
            if(ec)
            {
                std::println
                (
                    "Send failed for {}: {}",
                    endpoint,
                    ec.message()
                );
            }
        });
    }

    template<typename Handler>
    void Send(std::shared_ptr<Buffer> buffer, const asio::ip::udp::endpoint& endpoint, Handler&& handler)
    {
        m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), endpoint, 
        [buffer, callback = std::forward<Handler>(handler)](std::error_code ec, std::size_t length)
        {
            callback(ec, length);
        });
    }

    void ScheduleReceive()
    {
        m_Socket.async_receive_from
        (
            asio::buffer(m_ReceiveBuffer.Data(), m_ReceiveBuffer.Capacity()),
            m_RemoteEndpoint,
            [this](std::error_code error, std::size_t bytes)
            {
                if(error)
                {
                    if(error == asio::error::operation_aborted)
                        return;
                    else
                        std::println("Error receiving data: {}", error.message());
                }
                else if(bytes > 0)
                {
                    m_ReceiveCallback(m_RemoteEndpoint, m_ReceiveBuffer);
                }

                ScheduleReceive();
            }
        );
    }
};

}