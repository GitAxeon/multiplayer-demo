#pragma once

#include <chrono>
#include <functional>

#include <asio.hpp>

#include "Message.hpp"
#include "Transport.hpp"
#include "Sequencer.hpp"
#include "Reliability.hpp"
#include "ConnectionDebugInfo.hpp"
#include "Serialization/Serialization.hpp"

namespace Networking
{

class Connection : public std::enable_shared_from_this<Connection>
{
public:
    using Clock = std::chrono::steady_clock;

    // Use this thanks
    template<typename...Args>
    static std::shared_ptr<Connection> Create(Args&&... args)
    {
        return std::make_shared<Connection>(std::forward<Args>(args)...);
    }

    Connection(Transport& transport, const asio::ip::udp::endpoint& endpoint, std::uint32_t initialSequence, std::uint32_t initialRemoteSequence)
        : m_Transport(transport), m_Endpoint(endpoint)
    {
        auto& sequencer = m_Reliability.GetPacketSequencer();
        sequencer.SetSequence(initialSequence);
        sequencer.SetRemoteSequence(initialRemoteSequence);
    }
    
    // No copy
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // No move
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    ~Connection() = default;

    Clock::time_point GetLastSendTime() const { return m_LastSendTime; }
    Clock::time_point GetReceiveTime() const { return m_LastReceiveTime; }

    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        return m_Reliability.HandleIncoming(remoteSequence, acknowledge, acknowledgeBits);
    }

    bool Alive() const { return m_Alive; }

    void Send(std::span<const std::byte> data)
    {
        if(data.size() > 512u)
        {
            std::println("Connection Send(span<const byte>): data should be split in to multiple packets here");
        }

        const auto header = CreateHeader(MessageType::MESSAGE, UDP_Unreliable);
        auto buffer = Buffer::CreateShared(sizeof(Header) + data.size());
        
        StreamWriter serializer(*buffer);
        serializer.Write(header);
        serializer.Write(data);

        m_Transport.get().Send(buffer, m_Endpoint,[this](asio::error_code ec, auto)
        {
            if(!ec) m_LastSendTime = Clock::now();
        });
    }

    template<typename Handler>
    void Send(std::span<const std::byte> data, Handler&& handler)
    {
        if(data.size() > 512u)
        {
            std::println("Connection Send(span<const byte>): data should be split in to multiple packets here");
        }

        const auto header = CreateHeader(MessageType::MESSAGE, UDP_Unreliable);
        auto buffer = Buffer::CreateShared(sizeof(Header) + data.size());
        
        StreamWriter serializer(*buffer);
        serializer.Write(header);
        serializer.Write(data);

        m_Transport.get().Send(buffer, m_Endpoint,
            [this, callback = std::forward<Handler>(handler)](asio::error_code ec, std::size_t length)
            {
                if(!ec)
                {
                    m_LastSendTime = Clock::now();
                    callback(ec, length);
                }
            }
        );
    }

    void SendReliable(std::span<const std::byte> data)
    {
        if(data.size() > 512u)
        {
            std::println("Connection Send(span<const byte>): data should be split in to multiple packets here");
        }
        
        const auto header = CreateHeader(MessageType::MESSAGE, UDP_Reliable);

        auto buffer = Buffer::CreateShared(sizeof(Header) + data.size());
        
        StreamWriter serializer(*buffer);
        serializer.Write(header);
        serializer.Write(data);

        auto result = m_Reliability.AddMessage(buffer);

        if(result)
        {            
            m_Transport.get().Send(buffer, m_Endpoint,
                [this, sequence = result.value()](asio::error_code ec, std::size_t length)
            {
                if(!ec)
                {
                    m_Reliability.UpdateSendTime(sequence);
                    m_LastSendTime = Clock::now();
                }
            });
        }
    }

    template<typename Handler>
    void SendReliable(std::span<const std::byte> data, Handler&& handler)
    {
        if(data.size() > 512u)
        {
            std::println("Connection Send(span<const byte>): data should be split in to multiple packets here");
        }
        
        const auto header = CreateHeader(MessageType::MESSAGE, UDP_Reliable);
        auto buffer = Buffer::CreateShared(sizeof(Header) + data.size());
        
        StreamWriter serializer(*buffer);
        serializer.Write(header);
        serializer.Write(data);

        auto result = m_Reliability.AddMessage(buffer);

        if(result)
        {            
            m_Transport.get().Send(buffer, m_Endpoint,
                [this, sequence = result.value(), callback = std::forward<Handler>(handler)](asio::error_code ec, std::size_t length)
            {
                if(!ec)
                {
                    m_Reliability.UpdateSendTime(sequence);
                    m_LastSendTime = Clock::now();
                    callback(ec, length);
                }
            });
        }
    }

    void SendHeartbeat()
    {
        const auto header = CreateHeader(MessageType::HEARTBEAT, UDP_Reliable);
        
        auto buffer = Buffer::CreateShared(sizeof(Header));
        
        StreamWriter serializer(*buffer);
        serializer.Write(header);

        auto sequence = m_Reliability.AddMessage(buffer);

        if(sequence)
        {
            m_Transport.get().Send(buffer, m_Endpoint,[this, sequence = sequence.value()](asio::error_code ec, auto)
            {
                if(!ec)
                {
                    m_Reliability.UpdateSendTime(sequence);
                    m_LastSendTime = Clock::now();
                }
            });    
        }    
    }

    void SendConnectionAccepted()
    {
        const auto header = CreateHeader(MessageType::CONNECTION_ACCEPTED, UDP_Reliable);
        
        auto buffer = Buffer::CreateShared(sizeof(Header));
        StreamWriter serializer(*buffer);

        serializer.Write(header);

        auto sequence = m_Reliability.AddMessage(buffer);

        if(sequence)
        {
            // Maybe this should be shared_from_this() with necessary modifications
            m_Transport.get().Send
            (buffer, m_Endpoint,
            [this, sequence = sequence.value()]
            (asio::error_code ec, auto)
            {
                if(!ec)
                {
                    m_Reliability.UpdateSendTime(sequence);
                    m_LastSendTime = Clock::now();
                }
            });    
        }
    }

    // Note: The parameter 'now' currently serves no purpose really
    void Resend(std::chrono::steady_clock::time_point now)
    {
        using namespace std::chrono_literals;

        for(auto& [sequence, packet] : m_Reliability.GetPendingResends())
        {
            if(now - packet.lastSent >= 32ms)
            {
                m_Transport.get().Send(packet.packet, m_Endpoint, [this, sequence](asio::error_code ec, std::size_t length)
                {
                    if(!ec)
                    {
                        m_Reliability.UpdateSendTime(sequence);
                        m_Reliability.IncrementResendCount(sequence);
                    }
                    else
                    {
                        std::println("Error resending a reliable message: {}", ec.message());
                    }
                });
            }
        }
    }

    ConnectionDebugInfo GetDebufInfo() const
    {
        const auto now = Clock::now();
        
        const auto sequencer = m_Reliability.GetPacketSequencer();
        return 
        {
            .localEndpoint = m_Transport.get().LocalEndpoint(),
            .remoteEndpoint = m_Endpoint,
            .localSequence = sequencer.LocalSequence(),
            .remoteSequence = sequencer.RemoteSequence(),
            .acknowledgeBits = sequencer.AcknowledgeBits(),
            .timeSinceLastReceive = std::chrono::duration_cast<std::chrono::milliseconds>
            (
                now - m_LastReceiveTime
            ),
            .timeSinceLastSend = std::chrono::duration_cast<std::chrono::milliseconds>
            (
                now - m_LastSendTime
            )
        };
    }

private:
    Header CreateHeader(MessageType type, UDPFlag flags)
    {
        auto& sequencer = m_Reliability.GetPacketSequencer();

        return Header
        {
            .sequence = sequencer.ObtainNewSequence(),
            .acknowledged = sequencer.RemoteSequence(),
            .acknowledgeBits = sequencer.AcknowledgeBits(),
            .timestamp = TimeAsMilliseconds(),
            .flags = flags,
            .clientId = 0,
            .messageType = type
        };
    }

private:
    std::reference_wrapper<Transport> m_Transport;
    asio::ip::udp::endpoint m_Endpoint;

    ReliabilityLayer m_Reliability;

    Clock::time_point m_LastReceiveTime;
    Clock::time_point m_LastSendTime;
    
    bool m_Alive = true;
};

}