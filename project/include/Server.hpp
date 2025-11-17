#pragma once

#include <unordered_map>
#include <thread>

#include "Networking/Networking.hpp"
#include "Networking/AsioFormat.hpp"

namespace Networking
{

class Server
{
public:
    Server()
        : m_Context(), m_Transport(m_Context), m_Acceptor(m_Context, m_Transport),
        m_HeartbeatTimer(m_Context), m_ResendTimer(m_Context)
    {
        std::println("Server created");

        m_Transport.SetReceiveHandler([this](auto& event) -> void
        {
            OnReceiveData(event.from, event.data);
        });

        m_Acceptor.SetCallback([this](auto ec, auto connection)
        {
            std::println("New connection!");
            
            auto id = m_MonotonicClientId;
            m_MonotonicClientId++;

            m_Clients[id] = connection;

            SendHello(id);
        });
    }
    
    void SendHello(ClientId id)
    {
        const std::string message = "Hello";
        auto buffer = Buffer::CreateShared(sizeof(uint32_t) + message.length());
        StreamWriter serializer(*buffer);

        serializer.Write(message);

        SendReliable(id, buffer);
    }

    ~Server()
    {
        Stop();
        std::println("Server destructed");
    }

    asio::io_context& GetIOContext() { return m_Context; }

    bool Start(asio::ip::port_type port = 13998)
    {
        using namespace asio::ip;

        if(!m_Context.stopped() && m_NetworkThread.joinable())
        {
            std::println("Server already running");
            return false;
        }

        try
        {            
            m_Transport.Bind(udp::endpoint(udp::v4(), port));
            m_Transport.ScheduleReceive();

            std::println
            (
                "Server starting at {}",
                m_Transport.LocalEndpoint()
            );

            // ScheduleHeartbeat();
            ScheduleResend();

            m_NetworkThread = std::thread([this]()
            {
                try
                {
                    std::println("Network thread started");
                    m_Context.run();
                    std::println("Network thread stopped");
                }
                catch(std::exception& e)
                {
                    std::println("Network thread exception: {}", e.what());
                }
            });
            
            return true;
        }
        catch(std::exception& e)
        {
            std::println("Error: {}", e.what());
            return false;
        }
    }

    void OnReceiveData(const asio::ip::udp::endpoint& from, Buffer& data)
    {
        StreamReader deserializer(data);

        Header header;
        deserializer.Read(header);

        if(header.protocol != sProtocol)
        {
            std::println("Protocol mismatch in message header");
            return;
        }

        const bool connectionExists = m_Clients.find(header.clientId) != m_Clients.end();

        if(connectionExists)
        {
            ProcessMessage(from, data);
        }
        else
        {
            m_Acceptor.OnReceiveData(from, data);
        }
    }
    
    void ProcessMessage(const asio::ip::udp::endpoint& from, Buffer& data)
    {
        StreamReader deserializer(data);

        Header header;
        deserializer.Read(header);

        if(header.flags & UDP_Reliable)
        {
            auto clientIt = m_Clients.find(header.clientId);
            bool isNew = clientIt->second->HandleIncoming(header.sequence, header.acknowledged, header.acknowledgeBits);
            
            if(!isNew)
                return;
        }

        switch(header.messageType)
        {
        case MessageType::MESSAGE:
        {
            std::string message;
            deserializer.Read(message);

            std::println
            (
                "[{}] Received {} bytes from {}:{}: {}",
                header.timestamp,
                data.Size(),
                from.address().to_string(),
                from.port(),
                message
            );
        } break;
        case MessageType::DISCONNECT:
        {
            if(header.clientId == 0)
                return;

            auto clientIterator = m_Clients.find(header.clientId);
            
            if(clientIterator == m_Clients.end())
                return;
            
            std::println("Disconnect received from {}", header.clientId);
            m_Clients.erase(clientIterator);
        } break;
        }
    }

    void Stop()
    {
        if(!m_Context.stopped())
        {
            m_Context.stop();
        }
        
        if(m_NetworkThread.joinable())
            m_NetworkThread.join();
        
        std::println("UDPServer stopped");
    }

    void Broadcast(const std::string& message, bool reliable = false)
    {
        Header header;
        header.messageType = MessageType::MESSAGE;
        header.flags = !reliable ? UDP_Unreliable  : UDP_Reliable;
        header.timestamp = TimeAsMilliseconds();

        for(auto& [id, client] : m_Clients)
        {
            auto& reliability = client->GetReliabilityLayer().GetPacketSequencer();

            header.clientId = id;
            header.sequence = reliability.ObtainNewSequence();
            header.acknowledged = reliability.RemoteSequence();
            header.acknowledgeBits = reliability.AcknowledgeBits();
            
            auto buffer = Buffer::CreateShared(sizeof(Header) + sizeof(std::size_t) + message.length());

            StreamWriter serializer(*buffer);
            serializer.Write(header);
            serializer.Write(message);

            if(!reliable)
            {
                client->Send(buffer);
            }
            else
            {
                client->SendReliable(buffer);
            }
        }
    }

    void SendReliable(ClientId id, std::shared_ptr<Buffer> packet)
    {
        auto clientIterator = m_Clients.find(id);

        if(clientIterator == m_Clients.end())
            return;

        Header header = CreateReliableHeader(id, *(clientIterator->second));

        auto packetWithHeader = Buffer::CreateShared(sizeof(Header) + packet->Size());
        
        StreamWriter serializer(*packetWithHeader);
        serializer.Write(header);
        serializer.Write(*packet);

        clientIterator->second->SendReliable(packetWithHeader);
    }

private:
    void ScheduleHeartbeat()
    {
        using namespace std::chrono_literals;
        m_HeartbeatTimer.expires_after(1s);
        m_HeartbeatTimer.async_wait([&](std::error_code ec)
        {
            std::println("Could send heartbeat here?");
            ScheduleHeartbeat();
        });
    }
    
    void ScheduleResend()
    {
        using namespace std::chrono_literals;

        m_ResendTimer.expires_after(31ms);
        m_ResendTimer.async_wait([&](std::error_code ec)
        {
            if(ec == asio::error::operation_aborted)
                return; // Timer cancelled apparently >:(

            auto now = std::chrono::steady_clock::now();

            for(auto& [id, connection] : m_Clients)
            {
                connection->Resend(now);
                // if(now - connection.lastMessageTime > 1s) disconnect
            }

            ScheduleResend();
        });
    }

    Header CreateReliableHeader(ClientId id, Connection& client)
    {
        Header header;
        header.clientId = id;
        header.messageType = MessageType::MESSAGE;
        header.flags = UDP_Reliable;
        header.timestamp = TimeAsMilliseconds();

        auto& sequencer = client.GetReliabilityLayer().GetPacketSequencer();
        header.sequence = sequencer.ObtainNewSequence();
        header.acknowledged = sequencer.RemoteSequence();
        header.acknowledgeBits = sequencer.AcknowledgeBits();

        return header;
    }

private:
    asio::io_context m_Context;

    Transport m_Transport;
    std::thread m_NetworkThread;
    
    Acceptor m_Acceptor;

    std::unordered_map<ClientId, std::shared_ptr<Connection>> m_Clients;
    ClientId m_MonotonicClientId = 1;

    asio::steady_timer m_HeartbeatTimer;
    asio::steady_timer m_ResendTimer;
};    

}