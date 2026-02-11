#pragma once

#include <functional>
#include <print>

#include "Connection.hpp"
#include "../Random.hpp"

namespace Networking
{


class Connector
{
public:
    using ConnectCallback = std::function<void(asio::error_code, std::shared_ptr<Connection>)>;

    Connector(asio::io_context& context, Transport& transport)
        : m_Transport(transport), m_ResendTimer(context)
    {}

    bool Connected() const
    {
        return m_State == State::Connected;
    }

    void Connect(const asio::ip::udp::endpoint& endpoint, ConnectCallback connectCallback)
    {
        m_Endpoint = endpoint;
        m_ConnectCallback = std::move(connectCallback);

        m_Sequence = Random::RandomInt<uint32_t>();

        SendConnectionRequest([this](asio::error_code error, std::size_t length)
        {
            std::println("Sent connection request to {}", m_Endpoint);

            m_LastSend = std::chrono::steady_clock::now();
            m_State = State::SentConnectionRequest;
            m_ResendCount = 0;
        });

        ScheduleResend();
    }

    void OnReceiveData(const asio::ip::udp::endpoint& from, Buffer& data)
    {
        if(from != m_Endpoint)
        {
            std::println("Message received from {} who isn't the server {}", from, m_Endpoint);
            return;
        }

        std::println("Message received from the server. Who knows what's inside");

        StreamReader deserializer(data);

        Header header;
        deserializer.Read(header);

        switch(header.messageType)
        {
        case MessageType::CHALLENGE:
        {
            if(m_State == State::SentConnectionRequest)
            {
                HandleChallengeMessage(data);
            }
        } break;
        case MessageType::CONNECTION_ACCEPTED:
        {
            if(m_State == State::SentChallengeResponse)
            {
                ConnectionAccepted();
            }
        } break;
        default:
            break;
        }
    }

    asio::ip::udp::endpoint GetRemoteEndpoint() const
    {
        return m_Endpoint;
    }
    
private:
    void HandleChallengeMessage(Buffer& data)
    {
        if(m_State != State::SentConnectionRequest)
        {
            std::println("Unexpectedly received challenge from Server. Ignoring packet. HandshakeState: {}", static_cast<std::uint32_t>(m_State));
            return;
        }

        StreamReader deserializer(data);

        Header header;
        deserializer.Read(header);
        deserializer.Read(m_ServerChallenge);

        m_RemoteSequence = header.sequence;
        m_Sequence++;

        std::println("Challenge received from server: {}.", m_ServerChallenge);
        
        SendChallengeResponse([this](asio::error_code error, std::size_t length)
        {
            m_State = State::SentChallengeResponse;
            m_LastSend = std::chrono::steady_clock::now();
        });
        
        m_ResendCount = 0;
    }

    void ConnectionAccepted()
    {
        std::println("Connection established with server");

        m_State = State::Connected;
        auto connection = Connection::Create(m_Transport.get(), m_Endpoint, m_Sequence, m_RemoteSequence);
        m_ConnectCallback({}, connection);
    }

    template<typename Handler>
    void SendConnectionRequest(Handler&& handler)
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CONNECTION_REQUEST;
        header.sequence = m_Sequence;

        auto buffer = Buffer::CreateShared(sizeof(Header));
        
        StreamWriter serializer(*buffer);
        serializer.Write(header);

        m_Transport.get().Send(buffer, m_Endpoint, std::forward<Handler>(handler));
    }

    template<typename Handler>
    void SendChallengeResponse(Handler&& handler)
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.sequence = m_Sequence;
        header.acknowledged = m_RemoteSequence;
        header.messageType = MessageType::CHALLENGE_RESPONSE;
        
        auto buffer = Buffer::CreateShared(sizeof(Header) + sizeof(m_ServerChallenge));

        StreamWriter serializer(*buffer);
        serializer.Write(header);
        serializer.Write(m_ServerChallenge);

        m_Transport.get().Send(buffer, m_Endpoint, std::forward<Handler>(handler));
    }
    
    void ScheduleResend()
    {        
        using namespace std::chrono_literals;

        m_ResendTimer.expires_after(100ms);
        m_ResendTimer.async_wait([this](std::error_code ec)
        {
            if(ec)
            {
                std::println("[Error][HandShake]: steady_timer.async_wait: {}", ec.message());
                m_State = State::Disconnected;
                return;
            }

            if(m_State == State::Connected)
            {
                std::println("Connection has been established. Stopping resend.");
                return;
            }

            if(m_ResendCount >= MaxRetries)
            {
                std::println("Connection attempt to {} timed out.", m_Endpoint);
                m_State = State::Disconnected;
                return;
            }

            if(std::chrono::steady_clock::now() - m_LastSend < 200ms)
            {
                ScheduleResend();
                return;
            }

            switch(m_State)
            {
            case State::SentConnectionRequest:
            {
                SendConnectionRequest([this](asio::error_code error, std::size_t length)
                {
                    m_LastSend = std::chrono::steady_clock::now();
                    m_ResendCount++;
                });
            } break;
            case State::SentChallengeResponse:
            {
                SendChallengeResponse([this](asio::error_code error, std::size_t length)
                {
                    m_LastSend = std::chrono::steady_clock::now(); 
                    m_ResendCount++;
                });
                
            } break;
            default:
                break;
            }
            
            ScheduleResend();
        });
    }

private:
    enum class State
    { 
        Disconnected,
        SentConnectionRequest,
        SentChallengeResponse,
        Connected,
        Reconnecting
    };

private:
    std::reference_wrapper<Transport> m_Transport;
    asio::ip::udp::endpoint m_Endpoint;
    
    ConnectCallback m_ConnectCallback;

    State m_State = State::Disconnected;
    asio::steady_timer m_ResendTimer;
    std::chrono::steady_clock::time_point m_LastSend;
    uint32_t m_ServerChallenge = 0;

    uint32_t m_Sequence = 0;
    uint32_t m_RemoteSequence = 0;

    int m_ResendCount = 0;
    constexpr static int MaxRetries = 5;
};

}