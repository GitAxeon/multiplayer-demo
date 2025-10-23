#pragma once

#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>
#include <print>
#include <thread>
#include <memory>
#include <unordered_map>
#include <functional>

#if defined(__WIN32__)
    #include <SDKDDKVer.h>
#endif

#include <asio.hpp>

#include "NetworkBuffer.h"

namespace Networking
{

inline uint64_t TimeAsMilliseconds()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

enum class MessageType : uint8_t
{
    CONNECTION_REQUEST,
    CHALLENGE,
    CHALLENGE_RESPONSE,
    WELCOME,
    MESSAGE,
    ACKNOWLEDGE,
    DISCONNECT
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
        return m_SequenceNumber++;
    }

    uint32_t RemoteSequence() const
    {
        return m_RemoteSequenceNumber;
    }

    uint32_t AcknowledgeBits() const 
    {
        return m_AcknowledgeBits;
    }

    // Returns true if packet is new ie. not a duplicate
    bool RecordIncomingSequence(uint32_t sequenceNumber)
    {
        if(IsSequenceNewer(sequenceNumber, m_RemoteSequenceNumber))
        {        
            uint32_t diff = sequenceNumber - m_RemoteSequenceNumber;
            
            if(diff < 32)
            {
                m_AcknowledgeBits <<= diff;
            }
            else
            {
                m_AcknowledgeBits = 0;
            }

            m_AcknowledgeBits |= 1;
            m_RemoteSequenceNumber = sequenceNumber;

            return true;
        }
        else
        {
            uint32_t diff = m_RemoteSequenceNumber - sequenceNumber;

            if(diff >= 32)
            {
                return false;
            }

            uint32_t mask = 1u << diff;

            if(m_AcknowledgeBits & mask)
            {
                return false;
            }

            // m_AcknowledgeBits |= (1u << (diff - 1));
            m_AcknowledgeBits |= mask;
            return true;
        }
    }

    static bool IsSequenceNewer(uint32_t lhs, uint32_t rhs)
    {
        return static_cast<int32_t>(lhs - rhs) > 0;
    }

private:
    uint32_t m_SequenceNumber = 0;
    uint32_t m_RemoteSequenceNumber = 0;
    uint32_t m_AcknowledgeBits = 0;
};

constexpr int MaxBytesPerPacket = 1400;

using UDPFlag = uint8_t;

constexpr UDPFlag UDP_Unreliable = 1 << 0;
constexpr UDPFlag UDP_Reliable = 1 << 1;
constexpr UDPFlag UDP_OrderedReliable = 1 << 2;

using ClientId = uint8_t;

struct UDPHeader
{
    uint32_t protocol{sProtocol};
    uint32_t sequence{0};
    uint32_t acknowledged{0};
    uint32_t acknowledgeBits{0};
    uint64_t timestamp{0};
    UDPFlag flags{0};
    ClientId clientId{0};
    MessageType messageType;
};

struct ReliableMessage
{
    std::shared_ptr<Buffer> packet;
    uint32_t sequence = 0;
    std::chrono::steady_clock::time_point lastSent;

    int retries = 0;
    int m_MaxRetries = 32;
};

struct PendingClient
{
    using Clock = std::chrono::steady_clock;

    asio::ip::udp::endpoint endpoint;
    uint32_t challenge{0};
    Clock::time_point lastMessageTime;
    
    int retries = 5;
    int MaxRetries = 5;
};

struct ReliabilityLayer
{
    // Return true if the message hasn't been acknowledged before
    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        bool isNew = sequencer.RecordIncomingSequence(remoteSequence);
        
        if(!isNew)
            return false;

        UpdateResendBuffer(remoteSequence, acknowledgeBits);

        return true;
    }

    void UpdateResendBuffer(uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        for(auto it = resendBuffer.begin(); it != resendBuffer.end();)
        {
            uint32_t sequence = it->first;

            bool acknowledged = false;

            if(sequence == acknowledge)
            {
                acknowledged = true;
            }
            else if(PacketSequencer::IsSequenceNewer(acknowledge, sequence))
            {
                uint32_t diff = acknowledge - sequence;

                if(diff <= 32 && (acknowledgeBits & (1u << (diff - 1))) )
                {
                    acknowledged = true;
                }
            }

            if(acknowledged)
            {
                it = resendBuffer.erase(it);
            }
            else
            {
                it++;
            }
        }
    }
    
    PacketSequencer sequencer;
    std::unordered_map<uint32_t, ReliableMessage> resendBuffer;
};

struct UDPTransport
{
    using ReceiveCallback = std::function<void(const asio::ip::udp::endpoint&, Buffer&)>; 

    asio::ip::udp::socket m_Socket;
    ReceiveCallback m_ReceiveCallback;

    // Used when receiving data
    Buffer m_ReceiveBuffer{512};
    asio::ip::udp::endpoint m_RemoteEndpoint;
    
    UDPTransport(asio::io_context& context)
        : m_Socket(context)
    {}

    UDPTransport(asio::io_context& context, asio::ip::udp::endpoint endpoint)
        : m_Socket(context, endpoint)
    {}

    // No-copyable
    UDPTransport(const UDPTransport&) = delete;
    UDPTransport& operator=(const UDPTransport&) = delete;

    // Movable (thought it was spelled moveable)
    UDPTransport(UDPTransport&&) = default;
    UDPTransport& operator=(UDPTransport&&) = default;

    asio::ip::udp::endpoint LocalEndpoint()
    {
        return m_Socket.local_endpoint();
    }

    void SetReceiveCallback(ReceiveCallback callback)
    {
        m_ReceiveCallback = callback;
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

    void Send(std::shared_ptr<Buffer> buffer, asio::ip::udp::endpoint endpoint)
    {
        m_Socket.async_send_to(asio::buffer(buffer->Data(), buffer->Size()), endpoint, [buffer, endpoint](std::error_code ec, std::size_t length)
        {
            if(ec)
            {
                std::println
                (
                    "Send failed for {}:{}: {}",
                    endpoint.address().to_string(),
                    endpoint.port(),
                    ec.message()
                );
            } 
        });
    }

    void ScheduleReceive()
    {
        m_Socket.async_receive_from
        (
            asio::buffer(m_ReceiveBuffer.Data(), m_ReceiveBuffer.Capacity()),
            m_RemoteEndpoint,
            [this](std::error_code error, std::size_t bytes)
            {
                if(!error && bytes > 0)
                {
                    m_ReceiveCallback(m_RemoteEndpoint, m_ReceiveBuffer);
                }
                else if(error)
                {
                    std::println("Error receiving data: {}", error.message());
                }
                else
                {
                    // Zero bytes received so idk
                }

                ScheduleReceive();
            }
        );
    }
};

struct UDPConnection
{
    using Clock = std::chrono::steady_clock;

    UDPTransport& m_Transport;
    asio::ip::udp::endpoint endpoint;
    
    PacketSequencer sequencer;
    ReliabilityLayer reliability;

    Clock::time_point lastMessageTime;

    UDPConnection(UDPTransport& transport, const asio::ip::udp::endpoint& endpoint)
        : m_Transport(transport), endpoint(endpoint)
    {}

    UDPConnection(UDPTransport& transport)
        : m_Transport(transport)
    {}

    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        return reliability.HandleIncoming(remoteSequence, acknowledge, acknowledgeBits);
    }

    void Send(std::shared_ptr<Buffer> buffer)
    {
        m_Transport.Send(buffer, endpoint);
        lastMessageTime = Clock::now();
    }

    void SendReliable(std::shared_ptr<Buffer> buffer)
    {
        reliability.resendBuffer.emplace
        (
            std::piecewise_construct,
            std::forward_as_tuple(sequencer.CurrentSequence()),
            std::forward_as_tuple(buffer, sequencer.CurrentSequence())
        );

        Send(buffer);
        reliability.resendBuffer.at(sequencer.CurrentSequence()).lastSent = Clock::now();
    }

    void Resend(std::chrono::steady_clock::time_point now)
    {
        using namespace std::chrono_literals;

        for(auto& [sequence, packet] : reliability.resendBuffer)
        {                    
            if(Clock::now() - packet.lastSent >= 32ms)
            {
                Send(packet.packet);
                packet.lastSent = now;
                packet.retries++;
            }
        }
    }
};

class UDPAcceptor
{
public:
    UDPAcceptor(asio::io_context& context, UDPTransport& transport)
        : m_Context(context), m_Transport(transport)
    {}

    // No copy
    UDPAcceptor(const UDPAcceptor&) = delete;
    UDPAcceptor& operator=(UDPAcceptor&) = delete;
    
    // Movable
    UDPAcceptor(UDPAcceptor&&) = default;
    UDPAcceptor& operator=(UDPAcceptor&&) = default;

    asio::awaitable<UDPConnection> Accept()
    {
        // asio::co_spawn(m_Context, [this]
        // {

        // });
    }
    
    void OnReceiveData(const asio::ip::udp::endpoint& from, Buffer& buffer)
    {
        buffer.Reset();

        UDPHeader header;
        Deserialize(header, buffer);
        
        switch(header.messageType)
        {
            case MessageType::CONNECTION_REQUEST:
            {
                HandleConnectionRequest(from, buffer);
            } break;
            case MessageType::CHALLENGE_RESPONSE:
            {
                HandleChallengeResponse(from, buffer);
            } break;
        }
    }

    void HandleConnectionRequest(const asio::ip::udp::endpoint& from, Buffer& buffer)
    {
        auto clientIterator = m_PendingConnections.find(from);

        if(clientIterator == m_PendingConnections.end())
        {
            std::println("Received connection request from {}:{}", from.address().to_string(), from.port());
            
            /* Todo: Random generate */
            const uint32_t challenge = 123456;

            m_PendingConnections[from] = PendingClient
            {
                .endpoint = from,
                .challenge = challenge,
                .lastMessageTime = PendingClient::Clock::now()
            };
            
            SendChallenge(from);
        }
        else
        {
            std::println("Ignoring duplicate connection request from {}:{}", from.address().to_string(), from.port());
        }
    }

    void HandleChallengeResponse(const asio::ip::udp::endpoint& from, Buffer& buffer)
    {
        uint32_t challenge = 0;
        buffer.Read(challenge);

        std::println("Challenge response received from {}:{}: {}", from.address().to_string(), from.port(), challenge);

        auto pendingClient = m_PendingConnections.find(from);
        
        if(pendingClient == m_PendingConnections.end())
        {
            std::println
            (
                "Unexpectedly received challenge response from unknown client: {}:{}",
                from.address().to_string(),
                from.port()
            );

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

            m_PendingConnections.erase(pendingClient);

            return;
        }

        m_AcceptedConnections.emplace_back(pendingClient->first);

        m_PendingConnections.erase(pendingClient);
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

        for(auto it = m_PendingConnections.begin(); it != m_PendingConnections.end();)
        {
            auto& pendingClient = it->second;
            if(now - pendingClient.lastMessageTime > 200ms)
            {
                if(pendingClient.retries <= 1)
                {
                    it = m_PendingConnections.erase(it);
                    continue;
                }

                SendChallenge(pendingClient.endpoint);
                pendingClient.lastMessageTime = now;
                pendingClient.retries--;
            }

            it++;
        }
    }

    void SendChallenge(asio::ip::udp::endpoint endpoint)
    {
        const auto clientIterator = m_PendingConnections.find(endpoint);
        
        if(clientIterator == m_PendingConnections.end())
            return;

        UDPHeader header;
        header.messageType = MessageType::CHALLENGE;
        header.timestamp = TimeAsMilliseconds();

        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(uint32_t));

        Serialize(header, *buffer);
        buffer->Write(clientIterator->second.challenge);

        m_Transport.Send(buffer, endpoint);
    }

private:
    asio::io_context& m_Context;
    UDPTransport& m_Transport;

    std::unordered_map<asio::ip::udp::endpoint, PendingClient> m_PendingConnections;
    std::vector<asio::ip::udp::endpoint> m_AcceptedConnections;
    asio::steady_timer m_ChallengeTimer;
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
    UDPServer()
        : m_Context(), m_Transport(m_Context), m_ChallengeTimer(m_Context), m_HeartbeatTimer(m_Context), m_ResendTimer(m_Context)
    {
        std::println("UDPServer created");

        m_Transport.SetReceiveCallback([this](auto& from, auto& data) -> void
        {
            OnReceiveData(from, data);
        });
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
            m_Transport.Bind(udp::endpoint(udp::v4(), port));

            std::println
            (
                "Server starting at {}:{}",
                m_Transport.LocalEndpoint().address().to_string(),
                m_Transport.LocalEndpoint().port()
            );

            ScheduleChallengeCheck();
            ScheduleHeartbeat();
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
        UDPHeader header;
        Deserialize(header, data);

        if(header.protocol != sProtocol)
        {
            std::println("Protocol mismatch in message header");
            return;
        }

        if((header.flags & UDP_Reliable) && header.clientId != 0 && m_Clients.find(header.clientId) != m_Clients.end())
        {
            auto clientIt = m_Clients.find(header.clientId);
            bool isNew = clientIt->second.HandleIncoming(header.sequence, header.acknowledged, header.acknowledgeBits);
            
            if(!isNew)
                return;
        }

        switch(header.messageType)
        {
        case MessageType::CONNECTION_REQUEST:
        {
            auto clientIterator = m_PendingClients.find(from);

            if(clientIterator == m_PendingClients.end())
            {
                std::println("Received connection request from {}:{}", from.address().to_string(), from.port());
                
                /* Todo: Random generate */
                const uint32_t challenge = 123456;

                m_PendingClients[from] = PendingClient
                {
                    .endpoint = from,
                    .challenge = challenge,
                    .lastMessageTime = PendingClient::Clock::now()
                };
                
                SendChallenge(from);
            }
            else
            {
                std::println("Ignoring duplicate connection request from {}:{}", from.address().to_string(), from.port());
            }

        } break;
        case MessageType::CHALLENGE: { return; } break;
        case MessageType::CHALLENGE_RESPONSE:
        {
            uint32_t challenge = 0;
            data.Read(challenge);

            std::println("Challenge response received from {}:{}: {}", from.address().to_string(), from.port(), challenge);

            auto pendingClient = m_PendingClients.find(from);
            
            if(pendingClient == m_PendingClients.end())
            {
                // If connection has already been upgraded
                ClientId id = 0;
                data.Read(id);

                if(id && m_Clients.find(id) != m_Clients.end())
                {
                    SendWelcome(id);
                }
                else
                {
                    std::println
                    (
                        "Unexpectedly received challenge response from unknown client: {}:{}",
                        from.address().to_string(),
                        from.port()
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
            
            m_Clients.emplace
            (
                std::piecewise_construct,
                std::forward_as_tuple(m_MonotonicClientId),
                std::forward_as_tuple(m_Transport)
            );

            m_Clients[m_MonotonicClientId].endpoint = from;
            m_Clients[m_MonotonicClientId].lastMessageTime = UDPConnection::Clock::now();

            std::println("Upgraded {} from pending connection", m_MonotonicClientId);

            SendWelcome(m_MonotonicClientId);

            m_MonotonicClientId++;

        } break;
        case MessageType::MESSAGE:
        {
            std::string message;
            data.Read(message);

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
        UDPHeader header;
        header.messageType = MessageType::MESSAGE;
        header.flags = !reliable ? UDP_Unreliable  : UDP_Reliable;
        header.timestamp = TimeAsMilliseconds();

        for(auto& [id, client] : m_Clients)
        {
            header.clientId = id;
            header.sequence = client.sequencer.ObtainNewSequence();
            header.acknowledged = client.sequencer.RemoteSequence();
            header.acknowledgeBits = client.sequencer.AcknowledgeBits();
            
            auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(std::size_t) + message.length());

            Serialize(header, *buffer);
            buffer->Write(message);

            if(!reliable)
            {
                client.Send(buffer);
            }
            else
            {
                client.SendReliable(buffer);
            }
        }
    }

    void SendReliable(ClientId id, std::shared_ptr<Buffer> packet)
    {
        auto clientIterator = m_Clients.find(id);

        if(clientIterator == m_Clients.end())
            return;

        UDPHeader header = CreateReliableHeader(id, clientIterator->second);

        auto packetWithHeader = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + packet->Size());
        
        Serialize(header, *packetWithHeader);
        packetWithHeader->CopyFrom(*packet);

        clientIterator->second.SendReliable(packetWithHeader);
    }

private:
    // void SendChallenge(asio::ip::udp::endpoint endpoint)
    // {
    //     const auto clientIterator = m_PendingClients.find(endpoint);
        
    //     if(clientIterator == m_PendingClients.end())
    //         return;

    //     UDPHeader header;
    //     header.messageType = MessageType::CHALLENGE;
    //     header.timestamp = TimeAsMilliseconds();

    //     auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(uint32_t));

    //     Serialize(header, *buffer);
    //     buffer->Write(clientIterator->second.challenge);

    //     m_Transport.Send(buffer, endpoint);
    // }

    void SendWelcome(ClientId id)
    {
        const auto clientIterator = m_Clients.find(id);
        
        if(clientIterator == m_Clients.end())
            return;

        UDPHeader header = CreateReliableHeader(id, clientIterator->second);

        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(ClientId));

        Serialize(header, *buffer);
        buffer->Write(id);
        
        clientIterator->second.SendReliable(buffer); 
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
                connection.Resend(now);
                // if(now - connection.lastMessageTime > 1s) disconnect
            }

            ScheduleResend();
        });
    }

    UDPHeader CreateReliableHeader(ClientId id, UDPConnection& client)
    {
        UDPHeader header;
        header.clientId = id;
        header.messageType = MessageType::MESSAGE;
        header.flags = UDP_Reliable;
        header.sequence = client.sequencer.ObtainNewSequence();
        header.acknowledged = client.sequencer.RemoteSequence();
        header.acknowledgeBits = client.sequencer.AcknowledgeBits();
        header.timestamp = TimeAsMilliseconds();

        return header;
    }

private:
    asio::io_context m_Context;

    UDPTransport m_Transport;
    std::thread m_NetworkThread;

    std::unordered_map<asio::ip::udp::endpoint, PendingClient> m_PendingClients;
    std::unordered_map<ClientId, UDPConnection> m_Clients;
    ClientId m_MonotonicClientId = 1;

    asio::steady_timer m_ChallengeTimer;
    asio::steady_timer m_HeartbeatTimer;
    asio::steady_timer m_ResendTimer;
};

class UDPClient
{
public:
    UDPClient() : m_Context(), m_Transport(m_Context), m_Connection(m_Transport), m_Handshake(m_Context)
    {
        std::println("UDPClient constructed.");

        m_Transport.SetReceiveCallback([this](auto& from, auto& data)
        {
            OnReceiveData(from, data);
        });
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
            UDPHeader header;
            header.flags = !reliable ? UDP_Unreliable : UDP_Reliable;
            header.sequence = m_Connection.reliability.sequencer.ObtainNewSequence();
            header.acknowledged = m_Connection.reliability.sequencer.RemoteSequence();
            header.acknowledgeBits = m_Connection.reliability.sequencer.AcknowledgeBits();
            header.timestamp = TimeAsMilliseconds();

            header.clientId = m_ClientId;
            header.messageType = MessageType::MESSAGE;
            
            /* Create message containing data ie. serialize data */
            auto newBuffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(std::size_t) + message.size());
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
                m_Connection.reliability.resendBuffer[header.sequence].lastSent = UDPConnection::Clock::now();
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
        UDPHeader header;
        Deserialize(header, data);

        switch(header.messageType)
        {
        case MessageType::CHALLENGE:
        {
            if(m_Handshake.state != Handshake::State::SentJoin)
            {
                std::println("Unexpectedly received challenge from Server. Ignoring packet. HandshakeState: {}", static_cast<uint32_t>(m_Handshake.state));
                return;
            }

            data.Read(m_Handshake.serverChallenge);
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
                
                data.Read(m_ClientId);
            }

        } break;
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

    void SendJoin()
    {
        UDPHeader header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CONNECTION_REQUEST;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader));

        Serialize(header, *buffer);
        m_Connection.Send(buffer);
    }

    void SendChallengeResponse()
    {
        UDPHeader header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CHALLENGE_RESPONSE;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(UDPHeader) + sizeof(Handshake::serverChallenge));

        Serialize(header, *buffer);
        buffer->Write(m_Handshake.serverChallenge);

        m_Connection.Send(buffer);
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
                m_Handshake.Abort();
                std::println("[Error][HandShake]: steady_timer.async_wait: {}", ec.message());
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
            timer.cancel();
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
    std::thread m_NetworkThread;
    
    UDPTransport m_Transport;
    UDPConnection m_Connection;
    
    Handshake m_Handshake;

    ClientId m_ClientId = 0;
};

}