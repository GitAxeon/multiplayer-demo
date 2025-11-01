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
        m_ConnectCallback = connectCallback;

        SendConnectionRequest();
        m_LastResend = std::chrono::steady_clock::now();

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
                ConnectionAccepted(data);
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

        m_State = State::ReceivedChallenge;
        
        SendChallengeResponse();
        m_State = State::SentChallengeResponse;
        m_LastResend = std::chrono::steady_clock::now();

        std::println("Sent challenge response");
    }

    void ConnectionAccepted(Buffer& buffer)
    {
        std::println("Connection established with server");

        m_State = State::ConnectionAccepted; 
        m_LastResend = std::chrono::steady_clock::now();
    }

    void SendConnectionRequest()
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CONNECTION_REQUEST;
        
        auto buffer = Buffer::Create(sizeof(Header));
        Serialize(header, *buffer);

        m_Transport.Send(buffer, m_Endpoint);
    }

    void SendChallengeResponse()
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CHALLENGE_RESPONSE;
        
        auto buffer = Buffer::Create(sizeof(Header) + sizeof(m_ServerChallenge));

        Serialize(header, *buffer);
        buffer->Write(m_Handshake.serverChallenge);

        m_Transport.Send(buffer, m_Endpoint);
    }
    
    void ScheduleResend()
    {        
        using namespace std::chrono_literals;

        m_ResendTimer.expires_after(100ms);
        m_ResendTimer.async_wait([&](std::error_code ec)
        {
            if(ec)
            {
                std::println("[Error][HandShake]: steady_timer.async_wait: {}", ec.message());
                return;
            }

            if(m_State == State::Connected)
            {
                std::println("Connection has been established. Stopping resend.");
                return;
            }

            if(m_RetryCount >= MaxRetries)
            {
                std::println("Connection attempt to {} timed out.", m_Endpoint);
                return;
            }

            if(std::chrono::steady_clock::now() - m_LastResend < 200ms)
            {
                ScheduleResend();
                return;
            }

            switch(m_State)
            {
            case State::Disconnected:
            {
                SendConnectionRequest();
                m_LastResend = std::chrono::steady_clock::now();
                m_RetryCount++;
            } break;
            case State::ReceivedChallenge:
            {
                SendChallengeResponse();
                m_LastResend = std::chrono::steady_clock::now();
                m_RetryCount++;
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
        ReceivedChallenge,
        SentChallengeResponse,
        ConnectionAccepted,
        Connected,
        Reconnecting
    };

private:
    Transport& m_Transport;
    asio::ip::udp::endpoint m_Endpoint;
    
    ConnectCallback m_ConnectCallback;

    State m_State = State::Disconnected;
    asio::steady_timer m_ResendTimer;
    std::chrono::steady_clock::time_point m_LastResend;
    uint32_t m_ServerChallenge = 0;

    int m_RetryCount = 0;
    int MaxRetries = 5;
};

}