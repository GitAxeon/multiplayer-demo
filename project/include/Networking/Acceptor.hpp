#pragma once

#include <functional>

#include <asio.hpp>

#include "Transport.hpp"
#include "Connection.hpp"
#include "Random.hpp"

namespace Networking
{

struct PendingClient
{
    using Clock = std::chrono::steady_clock;

    asio::ip::udp::endpoint endpoint;
    uint32_t challenge{0};
    Clock::time_point lastMessageTime;
    uint32_t sequence = 0;
    uint32_t remoteSequence = 0;

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

    void SetCallback(AcceptCallback callback)
    {
        m_AcceptCallback = callback;
    }
    
    void OnReceiveData(const asio::ip::udp::endpoint& from, Buffer& buffer)
    {
        buffer.Reset();

        Header header;
        Deserialize(header, buffer);
        
        switch(header.messageType)
        {
            using enum MessageType;

            case CONNECTION_REQUEST:
            {
                HandleConnectionRequest(from, buffer);
            } break;
            case CHALLENGE_RESPONSE:
            {
                HandleChallengeResponse(from, buffer);
            } break;
            case ACKNOWLEDGE:
            {

            } break;
        }
    }

    void HandleConnectionRequest(const asio::ip::udp::endpoint& from, Buffer& buffer)
    {
        if(m_PendingConnections.find(from) == m_PendingConnections.end())
        {
            buffer.Reset();
            
            Header header;
            Deserialize(header, buffer);
            
            uint32_t remoteSequence = 0;
            
            try
            {
                buffer.Read(remoteSequence);
            }
            catch(const std::exception& e)
            {
                std::println("Buffer didn't contain sequence number. Dropping connection request.");
                return;
            }

            std::println("Received connection request from {}:{}", from.address().to_string(), from.port());
            
            const uint32_t challenge = Random::RandomInt<uint32_t>();

            m_PendingConnections[from] = PendingClient
            {
                .endpoint = from,
                .challenge = challenge,
                .lastMessageTime = PendingClient::Clock::now(),
                .sequence = Random::RandomInt<uint32_t>(),
                .remoteSequence = remoteSequence
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
        
        // This should be the second message from the client so I manually increment the sequence here
        connectionIterator->second.remoteSequence += 1;

        buffer.Reset();
        Header header;
        Deserialize(header, buffer);

        uint32_t challenge = 0;
        buffer.Read(challenge);

        std::println("Challenge response received from {}:{}: {}", from.address().to_string(), from.port(), challenge);

        if(header.sequence != connectionIterator->second.remoteSequence)
        {
            std::println
            (
                "Remote sequence number didn't match expected value. Expected {} but received {} instead. Pending connection dropped in response",
                connectionIterator->second.remoteSequence,
                header.sequence
            );

            m_PendingConnections.erase(connectionIterator);
            
            return;
        }

        if(header.acknowledged != connectionIterator->second.sequence)
        {
            std::println
            (
                "Remote acknowledge number didn't match expected value. Expected {} but received {} instead. Pending connection dropped in response",
                connectionIterator->second.sequence,
                header.acknowledged
            );
            
            m_PendingConnections.erase(connectionIterator);
            
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

        if(header.acknowledged != connectionIterator->second.sequence)
        {
            std::println
            (
                "Pending connections acknowledge field doesnt match expected value.\nAcknowledge: {}\nSequence: {}",
                header.acknowledged,
                connectionIterator->second.sequence
            );

            m_PendingConnections.erase(connectionIterator);
            
            return;
        }

        // Increment the local sequence for the next outgoing message
        connectionIterator->second.sequence += 1;
        
        auto connection = Connection
        (
            m_Transport,
            connectionIterator->first,
            connectionIterator->second.sequence,
            connectionIterator->second.remoteSequence
        );

        SendConnectionAccepted(connection);

        m_AcceptCallback({}, std::move(connection));

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
        header.sequence = clientIterator->second.sequence;
        header.timestamp = TimeAsMilliseconds();

        auto buffer = Buffer::Create(sizeof(Header) + sizeof(uint32_t));

        Serialize(header, *buffer);
        buffer->Write(clientIterator->second.challenge);

        m_Transport.Send(buffer, endpoint);
    }
    
    void SendConnectionAccepted(Connection& connection)
    {
        auto& sequencer = connection.GetReliabilityLayer().GetPacketSequencer();
        
        Header header;
        header.messageType = MessageType::CONNECTION_ACCEPTED;
        header.sequence = sequencer.ObtainNewSequence();
        header.acknowledged = sequencer.RemoteSequence();
        header.acknowledgeBits = sequencer.AcknowledgeBits();
        header.flags = UDP_Reliable;
        header.timestamp = TimeAsMilliseconds();
        
        auto buffer = Buffer::Create(sizeof(Header));

        Serialize(header, *buffer);
        
        connection.SendReliable(buffer); 
    }

private:
    asio::io_context& m_Context;
    Transport& m_Transport;

    std::unordered_map<asio::ip::udp::endpoint, PendingClient> m_PendingConnections;
    asio::steady_timer m_ChallengeTimer;

    AcceptCallback m_AcceptCallback;
};

}