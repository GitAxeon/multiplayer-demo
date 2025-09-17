#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>
#include <print>
#include <thread>
#include <memory>
#include <bitset>
#include <unordered_map>

#if defined(__WIN32__)
    #include <SDKDDKVer.h>
#endif

#include <asio.hpp>

#include "NetworkBuffer.h"

namespace Networking
{

enum class MessageType : uint8_t
{
    CONNECTION_REQUEST,
    CHALLENGE,
    CHALLENGE_RESPONSE,
    WELCOME,
    MESSAGE,
    ACKNOWLEDGE
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

using ClientId = uint8_t;

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

struct PendingClient
{
    using Clock = std::chrono::steady_clock;

    asio::ip::udp::endpoint endpoint;
    uint32_t challenge{0};
    Clock::time_point lastMessageTime;
    
    int retries = 5;
};

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
inline void Serialize(const UDPHeader& header, Buffer& buffer)
{
    buffer.Write(header.protocol);
    buffer.Write(header.sequence);
    buffer.Write(header.timestamp);
    buffer.Write(header.flags);
    buffer.Write(header.clientId);
    buffer.Write(header.messageType);
}

template<>
inline void Deserialize(UDPHeader& header, Buffer& buffer)
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
    UDPServer() : m_Context(), m_Socket(m_Context), m_ChallengeTimer(m_Context)
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
            ScheduleChallengeCheck();

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

        switch(header.messageType)
        {
        case MessageType::CONNECTION_REQUEST:
        {
            auto clientIterator = m_PendingClients.find(endpoint);

            if(clientIterator == m_PendingClients.end())
            {
                std::println("Received connection request from {}:{}", endpoint.address().to_string(), endpoint.port());
                
                /* Todo: Random generate */
                const uint32_t challenge = 123456;
                m_PendingClients[endpoint] = PendingClient
                {
                    .endpoint = endpoint,
                    .challenge = challenge,
                    .lastMessageTime = PendingClient::Clock::now()
                };
                
                SendChallenge(endpoint);
            }
            else
            {
                std::println("Ignoring duplicate connection request from {}:{}", endpoint.address().to_string(), endpoint.port());
            }

        } break;
        case MessageType::CHALLENGE: { return; } break;
        case MessageType::CHALLENGE_RESPONSE:
        {
            uint32_t challenge = 0;
            m_Buffer.Read(challenge);

            std::println("Challenge response received from {}:{}: {}", endpoint.address().to_string(), endpoint.port(), challenge);

            auto pendingClient = m_PendingClients.find(endpoint);
            
            if(pendingClient == m_PendingClients.end())
            {
                // If connection has already been upgraded
                ClientId id = 0;
                m_Buffer.Read(id);

                if(id && m_Clients.find(id) != m_Clients.end())
                {
                    SendWelcome(id);
                }
                else
                {
                    std::println
                    (
                        "Unexpectedly received challenge response from unknown client: {}:{}",
                        endpoint.address().to_string(),
                        endpoint.port()
                    );
                }

                return;
            }

            if(challenge != pendingClient->second.challenge)
            {
                std::println
                (
                    "Challenge response didn't match expected value. Expected {} but received {} instead. Pending connection dropped in response.",
                    pendingClient->second.challenge, 
                    challenge
                );

                m_PendingClients.erase(pendingClient);

                return;
            }

            m_PendingClients.erase(pendingClient);

            m_Clients[m_MonotonicClientId] = UDPConnection();
            m_Clients[m_MonotonicClientId].endpoint = endpoint;
            m_Clients[m_MonotonicClientId].lastMessageTime = UDPConnection::Clock::now();

            std::println("Upgraded {} from pending connection", m_MonotonicClientId);

            SendWelcome(m_MonotonicClientId);

            m_MonotonicClientId++;

        } break;
        case MessageType::MESSAGE:
        {
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
        } break;
        }
    }

    void Broadcast(const std::string& message, UDPFlag reliability = UDP_Unreliable)
    {
        UDPHeader header;

        const auto now = std::chrono::system_clock::now();
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        header.messageType = MessageType::MESSAGE;

        for(auto& [id, client] : m_Clients)
        {
            header.clientId = id;
            // header.sequence = client.sequencer.ObtainNewSequence();
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

        return true; 
    }

private:
    void SendChallenge(asio::ip::udp::endpoint endpoint)
    {
        const auto clientIterator = m_PendingClients.find(endpoint);
        
        if(clientIterator == m_PendingClients.end())
            return;

        UDPHeader header;
        header.messageType = MessageType::CHALLENGE;

        const auto now = std::chrono::system_clock::now();
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(uint32_t));

        Serialize(header, *buffer);
        buffer->Write(clientIterator->second.challenge);
        
        Send(endpoint, buffer);
    }

    void SendWelcome(ClientId id)
    {
        const auto clientIterator = m_Clients.find(id);
        
        if(clientIterator == m_Clients.end())
            return;

        UDPHeader header;
        header.messageType = MessageType::WELCOME;

        const auto now = std::chrono::system_clock::now();
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(ClientId));

        Serialize(header, *buffer);
        buffer->Write(id);
        
        Send(clientIterator->second.endpoint, buffer);        
    }

    void Send(ClientId clientId, std::shared_ptr<Networking::Buffer> buffer, UDPFlag reliable = UDP_Unreliable)
    {
        auto clientIterator = m_Clients.find(clientId);

        if(clientIterator == m_Clients.end())
        {
            std::println("Attempted to send message to an offline client with id {}", clientId);
            return;
        }
        
        Send(clientIterator->second.endpoint, buffer);
    }

    void Send(asio::ip::udp::endpoint endpoint, std::shared_ptr<Networking::Buffer> buffer)
    {
        m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), endpoint, [buffer, endpoint](std::error_code ec, std::size_t length)
        {
            if(ec)
            {
                std::println
                (
                    "Broadcast failed for {}:{}: {}",
                    endpoint.address().to_string(),
                    endpoint.port(),
                    ec.message()
                );
            }
        });        
    }

    void ScheduleChallengeCheck()
    {
        using namespace std::chrono_literals;
        m_ChallengeTimer.expires_after(100ms);

        m_ChallengeTimer.async_wait([&](std::error_code ec)
        {
            if(!ec)
            {
                UpdatePendingClients();
            }
            else
            {
                std::println("Error: steady_timer.async_wait: {}", ec.message());
            }

            ScheduleChallengeCheck();
        });
    }

    void UpdatePendingClients()
    {
        using namespace std::chrono_literals;
        auto now = PendingClient::Clock::now();

        for(auto it = m_PendingClients.begin(); it != m_PendingClients.end();)
        {
            auto& pendingClient = it->second;
            if(now - pendingClient.lastMessageTime > 200ms)
            {
                if(pendingClient.retries <= 1)
                {
                    it = m_PendingClients.erase(it);
                    continue;
                }

                SendChallenge(pendingClient.endpoint);
                pendingClient.lastMessageTime = now;
                pendingClient.retries--;
            }

            it++;
        }

    }

private:
    asio::io_context m_Context;
    asio::ip::udp::socket m_Socket;
    std::thread m_NetworkThread;

    asio::steady_timer m_ChallengeTimer;
    std::unordered_map<asio::ip::udp::endpoint, PendingClient> m_PendingClients;
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
    UDPClient() : m_Context(), m_Socket(m_Context), m_Handshake(m_Context)
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
            ScheduleReceive();
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
        
        m_Socket.close();
        
        std::println("UDPClient disconnected.");
    }

    void ScheduleReceive()
    {
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
                HandleDatagram();
            }
            else
            {
                std::println("Error: async_receive_from: {}", ec.message());
            }

            ScheduleReceive();
        });        
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

            header.clientId = m_ClientId;
            header.messageType = MessageType::MESSAGE;
            
            /* Create message containing data ie. serialize data */
            auto newBuffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(std::size_t) + message.size());
            Serialize(header, *newBuffer);
            newBuffer->Write(message);

            Send(newBuffer);

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
        m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), m_ServerAddress, [buffer](std::error_code ec, std::size_t length)
        {
            if(ec)
            {
                std::println("[Error] async_send_to: {}", ec.message());
            }
        });
    }

private:
    void HandleDatagram()
    {
        UDPHeader header;
        Deserialize(header, m_Buffer);

        switch(header.messageType)
        {
        case MessageType::CHALLENGE:
        {
            if(m_Handshake.state != Handshake::State::SentJoin)
            {
                std::println("Unexpectedly received challenge from Server. Ignoring packet. HandshakeState: {}", static_cast<uint32_t>(m_Handshake.state));
                return;
            }

            m_Buffer.Read(m_Handshake.serverChallenge);
            std::println("Challenge received from server: {}.", m_Handshake.serverChallenge);

            m_Handshake.Advance(Handshake::State::ReceivedChallenge);
            
            SendChallengeResponse();
            m_Handshake.Advance(Handshake::State::SentChallengeResponse);
            m_Handshake.lastMessageTime = std::chrono::steady_clock::now();

            std::println("Sent challenge response");
        } break;
        case MessageType::WELCOME:
        {
            if(m_Handshake.state == Handshake::State::SentChallengeResponse)
            {
                std::println("Connection established with server");
                m_Handshake.Advance(Handshake::State::ReceivedWelcome); 
                m_Handshake.lastMessageTime = std::chrono::steady_clock::now();
                
                m_Buffer.Read(m_ClientId);
            }

        } break;
        case MessageType::MESSAGE:
        {
            std::string message;
            m_Buffer.Read(message);
            
            std::println
            (
                "String received from server: {}.",
                message
            );
        } break;
        }
    }

    bool IsSequenceNewer(uint32_t lhs, uint32_t rhs)
    {
        return static_cast<int32_t>(lhs - rhs) > 0;
    }

    void SendJoin()
    {
        UDPHeader header;

        const auto now = std::chrono::system_clock::now();
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        header.messageType = MessageType::CONNECTION_REQUEST;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader));

        Serialize(header, *buffer);
        Send(buffer);
    }

    void SendChallengeResponse()
    {
        UDPHeader header;

        const auto now = std::chrono::system_clock::now();
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        header.messageType = MessageType::CHALLENGE_RESPONSE;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(Handshake::serverChallenge));

        Serialize(header, *buffer);
        buffer->Write(m_Handshake.serverChallenge);

        Send(buffer);        
    }
    
    void ScheduleJoinServer()
    {        
        using namespace std::chrono_literals;

        m_Handshake.timer.expires_after(100ms);
        m_Handshake.timer.async_wait([&](std::error_code ec)
        {
            if(!ec)
            {
                bool retry = true;

                switch(m_Handshake.state)
                {
                case Handshake::State::Offline:
                case Handshake::State::SentJoin:
                {
                    if(std::chrono::steady_clock::now() - m_Handshake.lastMessageTime >= 200ms)
                    {
                        SendJoin();
                        m_Handshake.lastMessageTime = std::chrono::steady_clock::now();
                        m_Handshake.Advance(Handshake::State::SentJoin);
                    }
                    
                } break;
                case Handshake::State::ReceivedChallenge:
                case Handshake::State::SentChallengeResponse:
                {
                    if(std::chrono::steady_clock::now() - m_Handshake.lastMessageTime >= 200ms)
                    {
                        if(m_Handshake.retries <= m_Handshake.m_MaxRetries)
                        {
                            m_Handshake.Advance(Handshake::State::SentChallengeResponse);

                            SendChallengeResponse();
                            m_Handshake.lastMessageTime = std::chrono::steady_clock::now();

                        }
                        else
                        {
                            std::println("Connection attempt timed out.");

                            m_Handshake.Abort();
                            retry = false;
                        }
                    }
                } break;
                case Handshake::State::ReceivedWelcome:
                {
                    // m_Handshake.retries = 5;
                    // m_Handshake.state = Handshake::State::ReceivedWelcome;
                    retry = false;
                } break;
                }

                if(retry)
                {
                    ScheduleJoinServer();
                }
            }
            else
            {
                m_Handshake.state = Handshake::State::Offline;
                m_Handshake.retries = 5;

                std::println("[Error]:[HandShake]: steady_timer.async_wait: {}", ec.message());
            }
        });
    }

private:
    struct Handshake
    {
        enum class State
        {
            Offline,
            SentJoin,
            ReceivedChallenge,
            SentChallengeResponse,
            ReceivedWelcome,
            Online
        };

        void Advance(State newState)
        {
            switch(state)
            {
            case State::Offline:
            {
                if(newState == State::SentJoin)
                {
                    state = State::SentJoin;
                    retries = 0;
                }
            } break;
            case State::SentJoin:
            {
                if(newState == State::ReceivedChallenge)
                {
                    state = State::ReceivedChallenge;
                }
                else if(newState == State::SentJoin)
                {
                    retries++;
                }

            } break;
            case State::ReceivedChallenge:
            {
                if(newState == State::SentChallengeResponse)
                {
                    state = State::SentChallengeResponse;
                    retries = 0;
                }
            } break;
            case State::SentChallengeResponse:
            {
                if(newState == State::ReceivedWelcome)
                {
                    state = State::ReceivedWelcome;
                    retries = 0;
                }
                else if(newState == State::SentChallengeResponse)
                {
                    retries++;
                }
            } break;
            }
        }

        void Abort()
        {
            state = State::Offline;
            retries = 0;
            serverChallenge = 0;
        }

        State state = State::Offline;
        uint32_t serverChallenge = 0;

        std::chrono::steady_clock::time_point lastMessageTime;
        int retries = 0;
        asio::steady_timer timer;

        int m_MaxRetries = 5;

        Handshake(asio::io_context& io) : timer(io) {}
    };

private:
    asio::io_context m_Context;
    asio::ip::udp::socket m_Socket;
    std::thread m_NetworkThread;
    asio::ip::udp::endpoint m_ServerAddress;
    
    Handshake m_Handshake;

    PacketSequencer m_PacketSequencer;

    uint32_t m_SendSequence = 0;
    uint32_t m_LastServerSequence = 0;
    std::bitset<1024>  m_ReceivedPackets;
    ClientId m_ClientId = 0;

    // Used for receiving
    asio::ip::udp::endpoint m_RemoteEndpoint;
    Buffer m_Buffer{512};
};

}