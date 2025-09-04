#pragma once

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

enum class MessageType
{
    CONNECTION_REQUEST
};

inline static uint32_t sProtocol {88173283};

using ClientID = uint8_t;

struct UDPConnection
{
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_LastMessage;
    asio::ip::udp::endpoint m_Endpoint;
    
    uint32_t lastReceivedSequence = 0;
    std::bitset<1024> receivedPackets;
};

using UDPFlag = uint8_t;

constexpr UDPFlag UDP_Reliable = 1 << 0;

struct UDPHeader
{
    uint32_t protocol {sProtocol};
    uint32_t sequence{0};
    uint64_t timestamp{0};
    UDPFlag flags{0};
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
}

template<>
void Deserialize(UDPHeader& header, Buffer& buffer)
{
    buffer.Read(header.protocol);
    buffer.Read(header.sequence);
    buffer.Read(header.timestamp);
    buffer.Read(header.flags);    
}

struct UDPMessage
{
    UDPHeader header;
    // std::vector<std::byte> data;
};

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
        header.sequence = m_CurrentSequence++;
        const auto now = std::chrono::system_clock::now();
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        for(const auto&[id, client] : m_NewClients)
        {
            auto buffer = std::make_shared<Networking::Buffer>(64);
            Serialize(header, *buffer);
            buffer->Write(message);
        
            m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), client.m_Endpoint, [buffer, client](std::error_code ec, std::size_t length)
            {
                if(ec)
                {
                    std::println("Broadcast failed for {}:{}", client.m_Endpoint.address().to_string(), client.m_Endpoint.port());
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
    
    std::unordered_map<ClientID, UDPConnection> m_NewClients;
    std::vector<UDPConnection> m_Clients;
    uint32_t m_CurrentSequence = 1;

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
        std::println("UDPClient preparing to receive data from any client.");

        m_Socket.async_receive_from(asio::buffer(m_ReceiveBuffer), m_RemoteEndpoint, [this](std::error_code ec, std::size_t length)
        {
            if(m_RemoteEndpoint != m_ServerAddress)
            {
                std::println("Received message from {}:{} who is not the server.", m_RemoteEndpoint.address().to_string(), m_RemoteEndpoint.port());

                ScheduleReceive();
                return;
            }

            if(!ec)
            {
                HandleDatagram(std::span<std::byte>(m_ReceiveBuffer.data(), length));
            }
            else
            {
                std::println("Error: async_receive_from: {}", ec.message());
            }

            ScheduleReceive();
        });        
    }

    void HandleDatagram(std::span<std::byte> data)
    {   
        std::string message(reinterpret_cast<char*>(data.data()), data.size());
        std::println
        (
            "Received {} bytes from {}:{}: {}",
            data.size(),
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
            /* Create message containing data ie. serialize data */
            auto newBuffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(std::size_t) + message.size());
            UDPHeader header;
            header.sequence = m_SendSequence++;
           
            const auto now = std::chrono::system_clock::now();
            header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            
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

    uint32_t m_SendSequence = 0;
    uint32_t m_LastServerSequence = 0;
    std::bitset<1024>  m_ReceivedPackets;

    asio::ip::udp::endpoint m_RemoteEndpoint;
    std::array<std::byte, 512> m_ReceiveBuffer;
    Buffer m_Buffer{512};
};

}