#pragma once

#include <thread>
#include <print>
#include <span>

#include "Networking/Connector.hpp"

namespace Networking
{

class Client
{
public:
    Client() : m_Context(), m_Transport(m_Context), m_Connector(m_Context, m_Transport)
    {
        std::println("UDPClient constructed.");

        m_Transport.SetReceiveHandler([this](auto& event)
        {
            OnReceiveData(event.from, event.data);
        });
    }
    
    ~Client()
    {
        Disconnect();
        std::println("UDPClient destructed.");
    }

    asio::io_context& GetIOContext() { return m_Context; }

    bool Connect(const asio::ip::udp::endpoint& remoteEndpoint)
    {
        using namespace asio::ip;

        if(!m_Context.stopped() && m_NetworkThread.joinable())
        {
            std::println("UDPServer already running.");
            return false;
        }

        try
        {
            m_Transport.Bind(udp::endpoint(udp::v4(), 0));
            m_Transport.ScheduleReceive();

            m_Connector.Connect(remoteEndpoint, [this](asio::error_code ec, std::shared_ptr<Connection> connection)
            {
                m_Connection = connection;
                std::println("Connection to server established");
            });

            std::println("Client created at {}", m_Transport.LocalEndpoint());

            m_NetworkThread = std::thread([this]()
            {
                try
                {
                    m_Context.run();
                }
                catch(std::exception & e)
                {
                    std::println("Network thread exception: {}", e.what());
                }
            });

            return true;
        }
        catch(const std::exception& e)
        {
            std::println("Error: {}", e.what());
            return false;
        }
    }

    void Disconnect()
    {
        if(!m_Context.stopped())
            m_Context.stop();
        
        if(m_NetworkThread.joinable())
            m_NetworkThread.join();
        
        std::println("UDPClient disconnected.");
    }

    bool Connected() const
    {
        return m_Connection && m_Connection->Alive();
    }

    void Send(std::span<const std::byte> data)
    {
        m_Connection->Send(data);
    }

    void Send(const std::string& message)
    {
        // The serializer only supports the custom buffer class so for the time being it's stack allocated
        // Also I would love to streamline setting the size of the buffer
        auto buffer = Buffer::CreateUnique(sizeof(std::uint32_t) + message.size());

        StreamWriter serializer(*buffer);
        serializer.Write(message);
        
        m_Connection->Send(std::span<const std::byte>{buffer->Data(), buffer->Size()});
    }

    bool SendReliable()
    {

    }

    std::optional<ConnectionDebugInfo> GetConnectionDebugInfo() const
    {
        if(!Connected())
            return std::nullopt;
            
        return m_Connection->GetDebufInfo();
    }

private:
    void OnReceiveData(const asio::ip::udp::endpoint& from, Buffer& data)
    {
        if(m_Connector.Connected())
        {
            ProcessMessage(from, data);
        }
        else
        {
            m_Connector.OnReceiveData(from, data);
        }
    }

    void ProcessMessage(const asio::ip::udp::endpoint& from, Buffer& data)
    {
        StreamReader deserializer(data);

        Header header;
        deserializer.Read(header);

        switch(header.messageType)
        {
        case MessageType::MESSAGE:
        {
            std::string message;
            deserializer.Read(message);
            
            std::println
            (
                "String received from server: {}",
                message
            );
        } break;
        case MessageType::HEARTBEAT:
        {
            std::println("Heartbeat received from server. Replying with a heartbeat");
            m_Connection->SendHeartbeat();
        } break;
        }
    }

private:
    asio::io_context m_Context;
    std::thread m_NetworkThread;
    
    Transport m_Transport;
    std::shared_ptr<Connection> m_Connection;
    Connector m_Connector;

    ClientId m_ClientId = 0;
};

}