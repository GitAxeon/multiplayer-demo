#pragma once

#include <functional>

#include "NetworkBuffer.hpp"
#include "Transport.hpp"
#include "Connection.hpp"

namespace Networking
{

class Connector
{
public:
    using ConnectCallback = std::function<void(asio::error_code, Connection)>;

    Connector(asio::io_context& context, Transport& transport)
        : m_Transport(transport), m_Handshake(context)
    {}

    void Connect(const asio::ip::udp::endpoint& endpoint, ConnectCallback connectCallback)
    {
        m_Endpoint = endpoint;
        m_ConnectCallback = connectCallback;
    }

    void OnReceiveData(const asio::ip::udp::endpoint& from, Buffer& data)
    {
        data.Reset();

        Header header;
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
        case MessageType::Connection_ACCEPTED:
        {
            if(m_Handshake.state == Handshake::State::SentChallengeResponse)
            {
                std::println("Connection established with server");
                m_Handshake.Advance(Handshake::State::ReceivedWelcome); 
                m_Handshake.lastMessageTime = std::chrono::steady_clock::now();
                
                data.Read(m_ClientId);
            }

        } break;
        }
    }

private:
    void SendJoin()
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CONNECTION_REQUEST;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(Header));

        Serialize(header, *buffer);
        m_Transport.Send(buffer, m_Endpoint);
    }

    void SendChallengeResponse()
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CHALLENGE_RESPONSE;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(Header) + sizeof(Handshake::serverChallenge));

        Serialize(header, *buffer);
        buffer->Write(m_Handshake.serverChallenge);

        m_Transport.Send(buffer, m_Endpoint);
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

                switch(m_Handshake.GetState())
                {
                case Handshake::State::Disconnected:
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
                case Handshake::State::ConnectionAccepted:
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
    public:
        enum class State
        {
            Disconnected,
            SentJoin,
            ReceivedChallenge,
            SentChallengeResponse,
            ConnectionAccepted,
            Connected
        };
        
        Handshake(asio::io_context& io) : timer(io) {}

        void Advance(State newState)
        {
            switch(m_State)
            {
            case State::Disconnected:
            {
                if(newState == State::SentJoin)
                {
                    m_State = State::SentJoin;
                    retries = 0;
                }
            } break;
            case State::SentJoin:
            {
                if(newState == State::ReceivedChallenge)
                {
                    m_State = State::ReceivedChallenge;
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
                    m_State = State::SentChallengeResponse;
                    retries = 0;
                }
            } break;
            case State::SentChallengeResponse:
            {
                if(newState == State::ConnectionAccepted)
                {
                    m_State = State::ConnectionAccepted;
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
            state = State::Disconnected;
            retries = 0;
            serverChallenge = 0;
            timer.cancel();
        }

        State GetState() const { return m_State; }

        State m_State = State::Disconnected;
        uint32_t serverChallenge = 0;

        std::chrono::steady_clock::time_point lastMessageTime;
        int retries = 0;
        asio::steady_timer timer;

        int m_MaxRetries = 5;
    };

private:
    Transport& m_Transport;
    Handshake m_Handshake;
    asio::ip::udp::endpoint m_Endpoint;
    ConnectCallback m_ConnectCallback;
};

}