#pragma once

#include <functional>

#include "NetworkBuffer.hpp"
#include "Transport.hpp"
#include "Connection.hpp"

#include "AsioFormat.hpp"

namespace Networking
{


class Connector
{
public:
    using ConnectCallback = std::function<void(asio::error_code, Connection)>;

    Connector(asio::io_context& context, Transport& transport)
        : m_Transport(transport), m_ResendTimer(context)
    {}

    void Connect(const asio::ip::udp::endpoint& endpoint, ConnectCallback connectCallback)
    {
        m_Endpoint = endpoint;
        m_ConnectCallback = std::move(connectCallback);

        SendConnectionRequest([this](asio::error_code error, std::size_t length)
        {
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

        data.Reset();

        Header header;
        Deserialize(header, data);

        switch(header.messageType)
        {
        case MessageType::CHALLENGE:
        {
            if(m_State == State::SentConnectionRequest)
                HandleChallengeMessage(data);
        } break;
        case MessageType::CONNECTION_ACCEPTED:
        {
            if(m_State == State::SentChallengeResponse)
                ConnectionAccepted();
        } break;
        }
    }

private:
    void HandleChallengeMessage(Buffer& data)
    {
        data.Reset();
        Header header;
        Deserialize(header, data);
        
        if(m_State != State::SentConnectionRequest)
        {
            std::println("Unexpectedly received challenge from Server. Ignoring packet. HandshakeState: {}", static_cast<uint32_t>(m_State));
            return;
        }

        data.Read(m_ServerChallenge);
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
        m_ConnectCallback({}, Connection(m_Transport, m_Endpoint));
    }

    template<typename Handler>
    void SendConnectionRequest(Handler&& handler)
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CONNECTION_REQUEST;
        
        auto buffer = Buffer::Create(sizeof(Header));
        Serialize(header, *buffer);

        m_Transport.Send(buffer, m_Endpoint, std::forward<Handler>(handler));
    }

    template<typename Handler>
    void SendChallengeResponse(Handler&& handler)
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CHALLENGE_RESPONSE;
        
        auto buffer = Buffer::Create(sizeof(Header) + sizeof(m_ServerChallenge));

        Serialize(header, *buffer);
        buffer->Write(m_ServerChallenge);

        m_Transport.Send(buffer, m_Endpoint, std::forward<Handler>(handler));
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
    Transport& m_Transport;
    asio::ip::udp::endpoint m_Endpoint;
    
    ConnectCallback m_ConnectCallback;

    State m_State = State::Disconnected;
    asio::steady_timer m_ResendTimer;
    std::chrono::steady_clock::time_point m_LastSend;
    uint32_t m_ServerChallenge = 0;

    uint32_t sequence = 0;
    uint32_t remoteSequence = 0;

    int m_ResendCount = 0;
    constexpr static int MaxRetries = 5;
};

}