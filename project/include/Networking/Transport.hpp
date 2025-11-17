#pragma once

#include <functional>
#include <print>
#include <queue>

#include <asio.hpp>

#include "AsioFormat.hpp" // IWYU pragma: keep
#include "Buffer.hpp"

namespace Networking
{

struct ReceiveEvent
{
    asio::ip::udp::endpoint from;
    Buffer& data;
    std::error_code errorCode;
};

class Transport
{
public:
    using SendHandler = std::function<void(asio::error_code, std::size_t)>;
    using ReceiveHandler = std::function<void(const ReceiveEvent&)>; 

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

    void SetReceiveHandler(ReceiveHandler handler)
    {
        m_ReceiveHandler = handler;
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

    std::size_t QueueLength() const { return m_SendQueue.size(); }

    void Send(std::shared_ptr<Buffer> buffer, const asio::ip::udp::endpoint& endpoint)
    {
        m_SendQueue.emplace(buffer, endpoint, [buffer, endpoint](asio::error_code ec, std::size_t length)
        {
            if(ec)
                std::println("Send failed for {}: {}", endpoint, ec.message());
        });

        if(!m_Sending)
            ProcessNextSend();
    }

    template<typename Handler>
    void Send(std::shared_ptr<Buffer> buffer, const asio::ip::udp::endpoint& endpoint, Handler&& handler)
    {
        m_SendQueue.emplace(buffer, endpoint, std::forward<Handler>(handler));
        
        if(!m_Sending)
            ProcessNextSend();

        // m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), endpoint, 
        // [buffer, callback = std::forward<Handler>(handler)](asio::error_code ec, std::size_t length) mutable
        // {
        //     callback(ec, length);
        // });
    }

    void ScheduleReceive()
    {
        m_Socket.async_receive_from
        (
            asio::buffer(m_ReceiveBuffer.Data(), m_ReceiveBuffer.Capacity()),
            m_RemoteEndpoint,
            [this](asio::error_code error, std::size_t bytesWritten)
            {
                if(error)
                {
                    if(error == asio::error::operation_aborted)
                        return;
                    else
                        std::println("Error receiving data: {}", error.message());
                }
                else if(bytesWritten > 0)
                {
                    m_ReceiveBuffer.SetSize(bytesWritten);

                    ReceiveEvent event
                    {
                        .from = m_RemoteEndpoint,
                        .data = m_ReceiveBuffer,
                        .errorCode = error
                    };

                    m_ReceiveHandler(event);
                }

                ScheduleReceive();
            }
        );
    }

private:
    void ProcessNextSend()
    {
        if(m_SendQueue.empty())
        {
            m_Sending = false;
            return;
        }

        m_Sending = true;

        auto& pending = m_SendQueue.front();
        asio::const_buffer constBuffer(pending.buffer->Data(), pending.buffer->Size());
        auto handler = pending.handler;

        m_Socket.async_send_to(constBuffer, pending.endpoint, 
        [this, handler, buff=pending.buffer](asio::error_code ec, std::size_t bytes)
        {
            handler(ec, bytes);
            m_SendQueue.pop();

            ProcessNextSend(); 
        });
    }

private:
    struct PendingSend
    {
        std::shared_ptr<Buffer> buffer;
        asio::ip::udp::endpoint endpoint;
        SendHandler handler;
    };

    asio::ip::udp::socket m_Socket;
    ReceiveHandler m_ReceiveHandler;

    std::queue<PendingSend> m_SendQueue;
    bool m_Sending = false;

    // Used when receiving data
    Buffer m_ReceiveBuffer{512};
    asio::ip::udp::endpoint m_RemoteEndpoint;
    
};

}