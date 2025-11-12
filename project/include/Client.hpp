#pragma once

#include <thread>
#include <vector>
#include <print>

#include "Networking/Networking.hpp"
#include "Networking/Connector.hpp"

namespace Networking
{

class Client
{
public:
    Client() : m_Context(), m_Transport(m_Context), m_Connection(m_Transport), m_Connector(m_Context, m_Transport)
    {
        std::println("UDPClient constructed.");

        m_Transport.SetReceiveCallback([this](auto& event)
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

            m_Connector.Connect(remoteEndpoint, [this](asio::error_code ec, Connection&& connection)
            {
                m_Connection = std::move(connection);
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

    bool Send(const std::string& message, bool reliable = false)
    {
        std::println("Attempting to send data to server.");

        try
        {
            auto& sequencer = m_Connection.GetReliabilityLayer().GetPacketSequencer();
            
            Header header;
            header.flags = !reliable ? UDP_Unreliable : UDP_Reliable;
            header.sequence = sequencer.ObtainNewSequence();
            header.acknowledged = sequencer.RemoteSequence();
            header.acknowledgeBits = sequencer.AcknowledgeBits();
            header.timestamp = TimeAsMilliseconds();

            header.clientId = m_ClientId;
            header.messageType = MessageType::MESSAGE;
            
            /* Create message containing data ie. serialize data */
            auto buffer = Buffer::CreateShared(sizeof(Header) + sizeof(std::size_t) + message.size());

            StreamWriter serializer(*buffer);
            serializer.Write(header);
            serializer.Write(message);

            m_Connection.SendReliable(buffer);

            return true;
        }
        catch(const std::exception& e)
        {
            std::println("[Error]:[UDPClient]: Send failed: {}", e.what());
            return false;
        }
    }

    void Send(std::shared_ptr<Networking::Buffer> buffer)
    {
        m_Connection.Send(buffer);
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
        }
    }
private:

private:
    asio::io_context m_Context;
    std::thread m_NetworkThread;
    
    Transport m_Transport;
    Connection m_Connection;
    Connector m_Connector;

    ClientId m_ClientId = 0;
};

}