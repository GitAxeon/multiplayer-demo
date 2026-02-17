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

namespace Networking
{

struct IncomingDatagram
{
    asio::ip::udp::endpoint endpoint;
    ByteVector data;
    asio::error_code error;
};

class AsyncSocket
{
public:
    // Return value is used to communicate if the socket should keep receiving more data 
    using ReceiveCallback = std::function<void(const IncomingDatagram&)>;

    using SendCallback = std::function<void(asio::error_code, std::size_t)>;

    explicit AsyncSocket(asio::io_context& ctx)
        : m_Socket(ctx) { }

    AsyncSocket(const AsyncSocket&) = delete;
    AsyncSocket& operator=(const AsyncSocket&) = delete;

    AsyncSocket(AsyncSocket&&) = delete;
    AsyncSocket& operator=(AsyncSocket&&) = delete;

    bool Bind(const asio::ip::udp::endpoint& endpoint)
    {
        if(m_Socket.is_open())
        {
            asio::error_code closeError;
            m_Socket.close(closeError);

            if(closeError)
            {
                std::println("Failed to close socket cleanly: {}", closeError.message());
                return false; // Could we continue despite the dirty close?
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

        m_Open = true;

        return true;
    }
    
    asio::ip::udp::endpoint LocalEndpoint() const { return m_Socket.local_endpoint(); }

    void SetReceiveCallback(ReceiveCallback callback) { m_ReceiveCallback = std::move(callback); }

    void Close()
    {
        if(!m_Open)
            return;

        asio::post(m_Socket.get_executor(), [this]()
        {
            CloseInternal();
        });
    } 

    void Send(std::span<const std::byte> buffer, const asio::ip::udp::endpoint& endpoint)
    {        
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

        asio::post(m_Socket.get_executor(), [this, p=std::move(pending)]() mutable
        {
            m_SendQueue.emplace(std::move(p));
            
            if(!m_Sending)
                ProcessNextSend();
        });
    }

    template<typename Callback>
    void Send(std::span<const std::byte> buffer, const asio::ip::udp::endpoint& endpoint, Callback&& callback)
    {        
        PendingSend pending
        {
            .endpoint = endpoint,
            .data = {buffer.begin(), buffer.end()},
            .callback = std::forward<Callback>(callback)
        };

        asio::post(m_Socket.get_executor(), [this, p=std::move(pending)]() mutable
        {
            m_SendQueue.emplace(std::move(p));
            
            if(!m_Sending)
                ProcessNextSend();
        });     
    }
    
    bool StartReceiving()
    {
        if(!m_Open)
            return false;

        asio::post(m_Socket.get_executor(), [this]()
        {
            if(m_Receiving)
                return;
            
            m_Receiving = true;
            ScheduleReceive();
        });

        return true;
    }
private:
    void ScheduleReceive()
    {
        m_Socket.async_receive_from
        (
            asio::buffer(m_ReceiveBuffer),
            m_RemoteEndpoint,
            [this](asio::error_code ec, std::size_t bytes)
            {
                if(!m_Receiving)
                    return;
                
                if(ec != asio::error::operation_aborted)
                    std::println("Error receiving data: {}", ec.message());
                else
                    return;
               
                IncomingDatagram msg
                {

                    .endpoint = m_RemoteEndpoint,
                    .data = {m_ReceiveBuffer.begin(), m_ReceiveBuffer.begin() + bytes},
                    .error = ec
                };

                if(m_ReceiveCallback)
                    m_ReceiveCallback(msg);
                
                ScheduleReceive();
            }
        );
    }

    void CloseInternal()
    {
        if(!m_Open)
            return;
        
        m_Open = false;

        m_Receiving = false;
        m_Sending = false;

        // ToDo: Check error codes
        asio::error_code ec;
        m_Socket.cancel(ec);
        m_Socket.close(ec);
    }

    void ProcessNextSend()
    {
        if(m_SendQueue.empty())
        {
            m_Sending = false;
            return;
        }

        m_Sending = true;

        auto pending = std::move(m_SendQueue.front());
        m_SendQueue.pop();

        m_Socket.async_send_to(asio::buffer(pending.data), pending.endpoint,
        [this, p = std::move(pending)](auto ec, auto bytes)
        {
            p.callback(ec, bytes);

            if(m_SendQueue.empty())
            {
                m_Sending = false;
            }
            else
            {
                // Note: Potential re-entrancy if using more than one thread and the ops are not serialized?
                ProcessNextSend();
            }
        });
    }

private:
    struct PendingSend
    {
        asio::ip::udp::endpoint endpoint;
        ByteVector data;
        SendCallback callback;
    };

private:
    asio::ip::udp::socket m_Socket;
    bool m_Open{false};

    // Sending
    std::queue<PendingSend> m_SendQueue;
    bool m_Sending{false};

    // Receiving
    std::vector<std::byte> m_ReceiveBuffer;
    asio::ip::udp::endpoint m_RemoteEndpoint;
    ReceiveCallback m_ReceiveCallback;

    bool m_Receiving{false};
};

}