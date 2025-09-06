#pragma once

#include <array>
#include <chrono>
#include <vector>
#include <print>
#include <thread>
#include <span>
#include <memory>
#include <bitset>
#include <unordered_map>
#include <functional>

#include <SDKDDKVer.h>
#include <asio.hpp>

#include "NetworkBuffer.h"

namespace Networking
{

enum class MessageType : uint8_t
{
    CONNECTION_REQUEST,
    CHALLENGE,
    CHALLENGE_RESPONSE
};

inline static uint32_t sProtocol {88173283U};

class PacketSequencer
{
public:
    uint32_t CurrentSequence() const
    {
        return m_SequenceNumber;
    }
    
    uint32_t ObtainNewSequence()
    {
        auto old = m_SequenceNumber;
        m_SequenceNumber++;
        return old;
    }

    uint32_t ExpectedSequence() const
    {
        return m_ExpectedSequenceNumber;
    }

    void UpdateExpectedSequence(uint32_t value)
    {
        m_ExpectedSequenceNumber = value;
    }

private:
    uint32_t m_SequenceNumber = 0;
    uint32_t m_ExpectedSequenceNumber = 0;
};

using UDPFlag = uint8_t;

constexpr UDPFlag UDP_Unreliable = 1 << 0;
constexpr UDPFlag UDP_Reliable = 1 << 1;
constexpr UDPFlag UDP_OrderedReliable = 1 << 2;

struct UDPHeader
{
    uint32_t protocol{sProtocol};
    uint32_t sequence{0};
    uint64_t timestamp{0};
    UDPFlag flags{0};
    ClientId clientId{0};
    MessageType messageType;
};

struct UDPMessage
{
    UDPHeader header;
    // std::vector<std::byte> data;
};

using ClientId = uint8_t;

struct UDPConnection
{
    using Clock = std::chrono::steady_clock;

    Clock::time_point lastMessageTime;
    asio::ip::udp::endpoint endpoint;
    
    PacketSequencer sequencer;
    std::array<UDPMessage, 256> resendBuffer;
    std::bitset<1024> receivedPackets;
};

template<typename T>
void Serialize(const T&, Buffer&)
{ 
    static_assert(sizeof(T) == 0, "Serialize<T> not implemented");
}

template<typename T>
void Deserialize(T&, Buffer&)
{
    static_assert(sizeof(T) == 0, "Deserialize<T> not implemented");
}

template<>
void Serialize(const UDPHeader& header, Buffer& buffer)
{
    buffer.Write(header.protocol);
    buffer.Write(header.sequence);
    buffer.Write(header.timestamp);
    buffer.Write(header.flags);
    buffer.Write(header.clientId);
    buffer.Write(header.messageType);
}

template<>
void Deserialize(UDPHeader& header, Buffer& buffer)
{
    buffer.Read(header.protocol);
    buffer.Read(header.sequence);
    buffer.Read(header.timestamp);
    buffer.Read(header.flags);
    buffer.Read(header.clientId);
    buffer.Read(header.messageType);
}

class UDPServer
{
public:
    UDPServer() : m_Context(), m_Socket(m_Context)
    {
        std::println("UDPServer created");
    }
    
    ~UDPServer()
    {
        Stop();
        std::println("UDPServer destructed");
    }

    asio::io_context& GetIOContext() { return m_Context; }

    bool Start(asio::ip::port_type port = 13998)
    {
        using namespace asio::ip;

        if(!m_Context.stopped() && m_NetworkThread.joinable())
        {
            std::println("UDPServer already running");
            return false;
        }

        try
        {
            m_Socket.open(udp::v4());
            m_Socket.bind(udp::endpoint(udp::v4(), port));

            std::println
            (
                "Server starting at {}:{}",
                m_Socket.local_endpoint().address().to_string(),
                m_Socket.local_endpoint().port()
            );

            m_Buffer.Clear();
            ScheduleReceive();

            m_NetworkThread = std::thread([this]()
            { 
                try
                {
                    std::println("Network thread started");
                    m_Context.run();
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

    void Stop()
    {
        if(!m_Context.stopped())
        {
            m_Context.stop();
        }
        
        if(m_NetworkThread.joinable())
            m_NetworkThread.join();
        
        m_Socket.close();
        
        std::println("UDPServer stopped");
    }

    void ScheduleReceive()
    {
        m_Buffer.Clear();

        m_Socket.async_receive_from(asio::buffer(m_Buffer.Data(), m_Buffer.Capacity()), m_RemoteEndpoint, [this](std::error_code ec, std::size_t length)
        {
            if(!ec)
            {
                HandleDatagram(m_RemoteEndpoint);
            }
            else
            {
                std::println("Error: async_receive_from: {}", ec.message());
            }

            ScheduleReceive();
        });
    }

    void HandleDatagram(const asio::ip::udp::endpoint& endpoint)
    { 
        UDPHeader header;
        Deserialize(header, m_Buffer);

        if(header.protocol != sProtocol)
        {
            std::println("Protocol mismatch in message header");
            return;
        }

        if(header.messageType == MessageType::CONNECTION_REQUEST)
        {
            if(m_Clients.find(header.clientId) != m_Clients.end())
            {
                std::println("Connection request from an already connected client?");
                return;
            }

            m_Clients[m_MonotonicClientId] = UDPConnection();
            m_Clients[m_MonotonicClientId].endpoint = endpoint;
            m_Clients[m_MonotonicClientId].lastMessageTime = UDPConnection::Clock::now();
            
            std::println(
                "New client [id: {}] from [{}:{}]",
                m_MonotonicClientId,
                endpoint.address().to_string(),
                endpoint.port()
            );

            m_MonotonicClientId++;

            return;
        }

        std::string message;
        m_Buffer.Read(message);

        std::println
        (
            "[{}] Received {} bytes from {}:{}: {}",
            header.timestamp,
            m_Buffer.Size(),
            endpoint.address().to_string(),
            endpoint.port(),
            message
        );
    }

    void Broadcast(const std::string& message, bool reliable = false)
    {
        UDPHeader header;

        const auto now = std::chrono::system_clock::now();
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        for(auto& [id, client] : m_Clients)
        {
            header.sequence = client.sequencer.ObtainNewSequence();
            auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(std::size_t) + message.length());

            Serialize(header, *buffer);
            buffer->Write(message);
        
            m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), client.endpoint, [buffer, client](std::error_code ec, std::size_t length)
            {
                if(ec)
                {
                    std::println("Broadcast failed for {}:{}", client.endpoint.address().to_string(), client.endpoint.port());
                }
            });
        }
    }

    bool PollMessage(UDPMessage& message)
    {
        if(m_IncomingMessages.empty())
            return false;
        
        m_IncomingMessageMutex.lock(); 
            
        message = *m_IncomingMessages.begin();
        m_IncomingMessages.erase(m_IncomingMessages.begin());
        
        m_IncomingMessageMutex.unlock();
    }

private:
    asio::io_context m_Context;
    asio::ip::udp::socket m_Socket;
    std::thread m_NetworkThread;

    std::unordered_map<ClientId, UDPConnection> m_PendingClients;
    std::unordered_map<ClientId, UDPConnection> m_Clients;
    ClientId m_MonotonicClientId = 1;

    std::mutex m_IncomingMessageMutex;
    std::vector<UDPMessage> m_IncomingMessages;
    
    // Used when receiving data. Only one receive happens at a time so no need for multiples
    asio::ip::udp::endpoint m_RemoteEndpoint;
    Buffer m_Buffer{512};
};

class UDPClient
{
public:
    UDPClient() : m_Context(), m_Socket(m_Context)
    {
        std::println("UDPClient constructed.");
    }
    
    ~UDPClient()
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
            m_Socket.open(udp::v4());
            m_Socket.bind(udp::endpoint(udp::v4(), 0));
            m_ServerAddress = remoteEndpoint;

            ScheduleReceive();
            std::println("Client created at {}:{}", m_Socket.local_endpoint().address().to_string(), m_Socket.local_endpoint().port());

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
        
        m_Socket.close();
        
        std::println("UDPClient disconnected.");
    }

    void ScheduleReceive()
    {
        std::println("UDPClient preparing to receive data from server.");
        m_Buffer.Clear();

        m_Socket.async_receive_from(asio::buffer(m_Buffer.Data(), m_Buffer.Capacity()), m_RemoteEndpoint, [this](std::error_code ec, std::size_t length)
        {
            if(m_RemoteEndpoint != m_ServerAddress)
            {
                std::println("Received message from {}:{} who is not the server.", m_RemoteEndpoint.address().to_string(), m_RemoteEndpoint.port());

                ScheduleReceive();
                return;
            }

            if(!ec)
            {
                // HandleDatagram(std::span<std::byte>(m_ReceiveBuffer.data(), length));
                HandleDatagram();
            }
            else
            {
                std::println("Error: async_receive_from: {}", ec.message());
            }

            ScheduleReceive();
        });        
    }

    void HandleDatagram()
    {
        UDPHeader header;
        Deserialize(header, m_Buffer);

        std::string message;
        m_Buffer.Read(message);
        
        std::println
        (
            "Received {} bytes from {}:{}: {}",
            m_Buffer.Size(),
            m_RemoteEndpoint.address().to_string(),
            m_RemoteEndpoint.port(),
            message
        );
    }

    bool Send(const std::string& message, bool reliable = false)
    {
        std::println("Attempting to send data to server.");

        try
        {
            UDPHeader header;
            header.sequence = m_SendSequence++;

            const auto now = std::chrono::system_clock::now();
            header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

            header.messageType = MessageType::CONNECTION_REQUEST;
            
            /* Create message containing data ie. serialize data */
            auto newBuffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(std::size_t) + message.size());
            Serialize(header, *newBuffer);
            newBuffer->Write(message);

            /* Send to the socket */
            m_Socket.async_send_to(asio::buffer(newBuffer->Data(), newBuffer->Size()), m_ServerAddress, [newBuffer](std::error_code ec, std::size_t length)
            {
                if(!ec)
                {
                    std::println("Data sent successfully.");
                }
                else
                {
                    std::println("Error: async_send_to: {}", ec.message());
                }
            });

            return true;
        }
        catch(const std::exception& e)
        {
            std::println("UDPClient::Send failed: {}", e.what());
            return false;
        }
    }
    
    bool IsSequenceNewer(uint32_t lhs, uint32_t rhs)
    {
        return static_cast<int32_t>(lhs - rhs) > 0;
    }

private:
    asio::io_context m_Context;
    asio::ip::udp::socket m_Socket;
    std::thread m_NetworkThread;
    asio::ip::udp::endpoint m_ServerAddress;

    PacketSequencer m_PacketSequencer;

    uint32_t m_SendSequence = 0;
    uint32_t m_LastServerSequence = 0;
    std::bitset<1024>  m_ReceivedPackets;

    // Used for receiving
    asio::ip::udp::endpoint m_RemoteEndpoint;
    Buffer m_Buffer{512};
};

}