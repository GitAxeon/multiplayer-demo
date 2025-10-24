#pragma once

#include <thread>
#include <vector>
#include <print>

#include "Networking/Networking.hpp"

namespace Networking
{

class Client
{
public:
    Client() : m_Context(), m_Transport(m_Context), m_Connection(m_Transport), m_Handshake(m_Context)
    {
        std::println("UDPClient constructed.");

        m_Transport.SetReceiveCallback([this](auto& from, auto& data)
        {
            OnReceiveData(from, data);
        });
    }
    
    ~Client()
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
            Header header;
            header.flags = !reliable ? UDP_Unreliable : UDP_Reliable;
            header.sequence = m_Connection.reliability.sequencer.ObtainNewSequence();
            header.acknowledged = m_Connection.reliability.sequencer.RemoteSequence();
            header.acknowledgeBits = m_Connection.reliability.sequencer.AcknowledgeBits();
            header.timestamp = TimeAsMilliseconds();

            header.clientId = m_ClientId;
            header.messageType = MessageType::MESSAGE;
            
            /* Create message containing data ie. serialize data */
            auto newBuffer = std::make_shared<Networking::Buffer>(sizeof(Header) + sizeof(std::size_t) + message.size());
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
                m_Connection.reliability.resendBuffer[header.sequence].lastSent = Connection::Clock::now();
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
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CONNECTION_REQUEST;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(Header));

        Serialize(header, *buffer);
        m_Connection.Send(buffer);
    }

    void SendChallengeResponse()
    {
        Header header;
        header.timestamp = TimeAsMilliseconds();
        header.messageType = MessageType::CHALLENGE_RESPONSE;
        
        auto buffer = std::make_shared<Networking::Buffer>(sizeof(Header) + sizeof(Handshake::serverChallenge));

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
    
    Transport m_Transport;
    Connection m_Connection;
    
    Handshake m_Handshake;

    ClientId m_ClientId = 0;
};

}