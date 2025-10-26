#pragma once

#include <functional>

#include <asio.hpp>

#include "Transport.hpp"

namespace Networking
{

struct PendingClient
{
    using Clock = std::chrono::steady_clock;

    asio::ip::udp::endpoint endpoint;
    uint32_t challenge{0};
    Clock::time_point lastMessageTime;
    
    int retries = 5;
    int MaxRetries = 5;
};

class Acceptor
{
public:
    using AcceptCallback = std::function<void(asio::error_code, Connection)>; 

    Acceptor(asio::io_context& context, Transport& transport)
        : m_Context(context), m_Transport(transport), m_ChallengeTimer(context)
    {}

    // No copy
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(Acceptor&) = delete;
    
    // Movable
    Acceptor(Acceptor&&) = default;
    Acceptor& operator=(Acceptor&&) = default;

    void Accept(AcceptCallback callback)
    {
        if(!m_AcceptedConnections.empty())
        {
            
        }
        else
        {
            m_AcceptCallback = callback;
        }
    }
    
    void OnReceiveData(const asio::ip::udp::endpoint& from, Buffer& buffer)
    {
        buffer.Reset();

        Header header;
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
            case MessageType::ACKNOWLEDGE:
            {

            } break;
        }
    }

    void HandleConnectionRequest(const asio::ip::udp::endpoint& from, Buffer& buffer)
    {
        if(m_PendingConnections.find(from) == m_PendingConnections.end())
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

        auto connectionIterator = m_PendingConnections.find(from);
        
        if(connectionIterator == m_PendingConnections.end())
        {
            std::println
            (
                "Unexpectedly received challenge response from unknown client: {}:{}",
                from.address().to_string(),
                from.port()
            );

            return;
        }

        if(challenge != connectionIterator->second.challenge)
        {
            std::println
            (
                "Challenge response didn't match expected value. Expected {} but received {} instead. Pending connection dropped in response.",
                connectionIterator->second.challenge, 
                challenge
            );

            m_PendingConnections.erase(connectionIterator);

            return;
        }

        // m_AcceptedConnections.emplace_back(connectionIterator->first);
        auto& connection = m_AcceptedConnections.emplace_back
        (
            m_Transport,
            connectionIterator->first
        );

        m_PendingConnections.erase(connectionIterator);
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

        Header header;
        header.messageType = MessageType::CHALLENGE;
        header.timestamp = TimeAsMilliseconds();

        auto buffer = std::make_shared<Networking::Buffer>(sizeof(Header) + sizeof(uint32_t));

        Serialize(header, *buffer);
        buffer->Write(clientIterator->second.challenge);

        m_Transport.Send(buffer, endpoint);
    }
    
    void SendWelcome(ClientId id)
    {
        // const auto clientIterator = m_Clients.find(id);
        
        // if(clientIterator == m_Clients.end())
        //     return;

        // Header header = CreateReliableHeader(id, clientIterator->second);

        // auto buffer = std::make_shared<Networking::Buffer>(sizeof(Header) + sizeof(ClientId));

        // Serialize(header, *buffer);
        // buffer->Write(id);
        
        // clientIterator->second.SendReliable(buffer); 
    }
private:
    asio::io_context& m_Context;
    Transport& m_Transport;

    std::unordered_map<asio::ip::udp::endpoint, PendingClient> m_PendingConnections;
    std::vector<Connection> m_AcceptedConnections;
    asio::steady_timer m_ChallengeTimer;

    AcceptCallback m_AcceptCallback;
};

}