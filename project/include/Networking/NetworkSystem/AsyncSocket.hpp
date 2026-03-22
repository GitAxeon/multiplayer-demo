#pragma once

#include <vector>
#include <cstddef>
#include <span>
#include <print> 
#include <memory>
#include <vector>
#include <queue>

#include <asio.hpp>

#include "../AsioFormat.hpp"
#include "Buffer.hpp"
#include "Error.hpp"
#include "Types.hpp"
#include "SocketContext.hpp"

namespace Networking
{

struct IncomingDatagram
{
    asio::ip::udp::endpoint endpoint;
    ByteVector data;
    TransportError error;
};

class AsyncSocket
{
public:
    // Return value is used to communicate if the socket should keep receiving more data 
    using ReceiveCallback = std::function<void(IncomingDatagram const&)>;

    using SendCallback = std::function<void(asio::error_code, std::size_t)>;

    struct PendingSend
    {
        asio::ip::udp::endpoint endpoint;
        ByteVector data;
        SendCallback callback;
    };

    explicit AsyncSocket(SocketHandle handle, asio::io_context& ioContext, SocketContext socketContext)
        : m_Handle(handle), m_Socket(ioContext), m_Context(socketContext), m_ReceiveBuffer(512, std::byte{0})
    { }

    AsyncSocket(AsyncSocket const&) = delete;
    AsyncSocket& operator=(AsyncSocket const&) = delete;

    AsyncSocket(AsyncSocket&&) = default;
    AsyncSocket& operator=(AsyncSocket&&) = default;

    TransportError Bind(asio::ip::udp::endpoint const& endpoint)
    {
        if(m_Socket.is_open())
        {
            asio::error_code shutdownError;
            m_Socket.shutdown(m_Socket.shutdown_both, shutdownError);
            if(shutdownError)
            {
                return TranslateError(shutdownError);
            }

            asio::error_code closeError;
            m_Socket.close(closeError);
            if(closeError)
            {
                return TranslateError(closeError); // Could we continue despite the dirty close?
            }
        }

        asio::error_code openError;
        m_Socket.open(endpoint.protocol(), openError);
        
        if(openError)
        {
            std::println("Failed to open socket: {}", openError.message());
            return TranslateError(openError);
        }

        asio::error_code bindError;
        m_Socket.bind(endpoint, bindError);

        if(bindError)
        {
            std::println("Failed to bind socket: {}", bindError.message());
            return TranslateError(bindError);
        }

        m_Open = true;

        return TransportError::None;
    }
    
    asio::ip::udp::endpoint LocalEndpoint() const { return m_Socket.local_endpoint(); }

    void SetReceiveCallback(ReceiveCallback callback) { m_ReceiveCallback = std::move(callback); }

    void Close()
    {
        if(!m_Open)
            return;

        m_Open = false;

        m_ReceiveEnabled = false;
        m_IsSending = false;
        
        asio::error_code ec;
        m_Socket.shutdown(asio::socket_base::shutdown_both, ec);
        m_Socket.close(ec);
    }

    void Send(std::span<const std::byte> buffer, asio::ip::udp::endpoint const& endpoint)
    {
        if(!m_Open)
            return;

        PendingSend pending
        {
            .endpoint = endpoint,
            .data = {buffer.begin(), buffer.end()},
            .callback = [endpoint](auto ec, auto size)
            {
                if(ec)
                {
                    std::println("Send failed for {}: {}", endpoint, ec.message());
                }
            }
        };

        m_SendQueue.emplace(std::move(pending));
        
        if(!m_IsSending)
            ProcessNextSend();
    }

    template<typename Callback>
    void Send(std::span<const std::byte> buffer, const asio::ip::udp::endpoint& endpoint, Callback&& callback)
    {        
        if(!m_Open)
            return;

        PendingSend pending
        {
            .endpoint = endpoint,
            .data = {buffer.begin(), buffer.end()},
            .callback = std::forward<Callback>(callback)
        };

        m_SendQueue.emplace(std::move(pending));
        
        if(!m_IsSending)
            ProcessNextSend();
    }
    
    bool StartReceiving()
    {
        if(!m_Open)
            return false;

        if(m_ReceiveEnabled)
            return false;
        
        m_ReceiveEnabled = true;
        ScheduleReceive();

        return true;
    }

    void OnReceive(asio::error_code ec, std::size_t bytes)
    {
        if(ec == asio::error::operation_aborted)
        {
            std::println("Socket async operation aborted, returning");
            return;
        }
        
        IncomingDatagram msg
        {
            .endpoint = m_RemoteEndpoint,
            .data = {m_ReceiveBuffer.begin(), m_ReceiveBuffer.begin() + bytes},
            .error = TranslateError(ec)
        };

        if(m_ReceiveCallback)
            m_ReceiveCallback(msg);
        
        if(CanReceive())
            ScheduleReceive();       
    }

    void OnSendComplete(PendingSend&& pending, asio::error_code ec, std::size_t bytes)
    {
        pending.callback(ec, bytes);

        if(ec == asio::error::operation_aborted)
            return;

        if(m_SendQueue.empty())
        {
            m_IsSending = false;
        }
        else
        {
            // Note: Potential re-entrancy if using more than one thread and the ops are not serialized?
            ProcessNextSend();
        }        
    }

private:
    void ScheduleReceive()
    {
        if(!CanReceive())
            return;
        
        m_Socket.async_receive_from
        (
            asio::buffer(m_ReceiveBuffer),
            m_RemoteEndpoint,
            [context = m_Context, handle = m_Handle](asio::error_code ec, std::size_t bytes) mutable
            {
                if(auto self = context.Get(handle))
                    self->OnReceive(ec, bytes);
            }
        );
    }

    void ProcessNextSend()
    {
        if(m_SendQueue.empty())
        {
            m_IsSending = false;
            return;
        }

        m_IsSending = true;

        auto pending = std::move(m_SendQueue.front());
        m_SendQueue.pop();

        auto buffer = asio::buffer(pending.data);
        auto endpoint = pending.endpoint;

        m_Socket.async_send_to(buffer, endpoint,
        [handle = m_Handle, context = m_Context, p = std::move(pending)](auto ec, auto bytes) mutable
        {
            if(auto self = context.Get(handle))
                self->OnSendComplete(std::move(p), ec, bytes);
        });
    }
    
    bool CanReceive() const
    {
        return m_Open && m_ReceiveEnabled;
    }

private:
    SocketHandle m_Handle;
    asio::ip::udp::socket m_Socket;
    SocketContext m_Context;

    bool m_Open{false};

    // Sending
    std::queue<PendingSend> m_SendQueue;
    bool m_IsSending{false};

    // Receiving
    // Note: If receive buffer isnt resized no data can be received since it's capacity? is 0
    std::vector<std::byte> m_ReceiveBuffer;
    asio::ip::udp::endpoint m_RemoteEndpoint;
    ReceiveCallback m_ReceiveCallback;

    bool m_ReceiveEnabled{false};
};

}