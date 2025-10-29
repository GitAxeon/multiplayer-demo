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
    Client() : m_Context(), m_Transport(m_Context), m_Connection(m_Transport)
    {
        std::println("UDPClient constructed.");

        m_Transport.SetReceiveCallback([this](auto& from, auto& data)
        {
            OnReceiveData(from, data);
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
            m_Connection.endpoint = remoteEndpoint;

            std::println("Client created at {}:{}", m_Transport.LocalEndpoint().address().to_string(), m_Transport.LocalEndpoint().port());

            ScheduleJoinServer();

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
            Header header;
            header.flags = !reliable ? UDP_Unreliable : UDP_Reliable;
            header.sequence = m_Connection.reliability.sequencer.ObtainNewSequence();
            header.acknowledged = m_Connection.reliability.sequencer.RemoteSequence();
            header.acknowledgeBits = m_Connection.reliability.sequencer.AcknowledgeBits();
            header.timestamp = TimeAsMilliseconds();

            header.clientId = m_ClientId;
            header.messageType = MessageType::MESSAGE;
            
            /* Create message containing data ie. serialize data */
            auto newBuffer = std::make_shared<Networking::Buffer>(sizeof(Header) + sizeof(std::size_t) + message.size());
            Serialize(header, *newBuffer);
            newBuffer->Write(message);

            if(reliable)
            {
                m_Connection.reliability.resendBuffer[header.sequence] = ReliableMessage
                {
                    newBuffer,
                    header.sequence
                };
            }

            m_Connection.Send(newBuffer);

            if(reliable)
            {
                m_Connection.reliability.resendBuffer[header.sequence].lastSent = Connection::Clock::now();
            }

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
    void OnReceiveData(const asio::ip::udp::endpoint& server, Buffer& data)
    {
        Header header;
        Deserialize(header, data);

        switch(header.messageType)
        {
        case MessageType::MESSAGE:
        {
            std::string message;
            data.Read(message);
            
            std::println
            (
                "String received from server: {}.",
                message
            );
        } break;
        case MessageType::ACKNOWLEDGE:
        {

        } break;
        }
    }

private:

private:
    asio::io_context m_Context;
    std::thread m_NetworkThread;
    
    Transport m_Transport;
    Connection m_Connection;

    ClientId m_ClientId = 0;
};

}