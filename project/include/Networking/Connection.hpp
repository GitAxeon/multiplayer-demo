#pragma once

#include <chrono>
#include <cstdint>

#include <asio.hpp>

#include "Transport.hpp"
#include "Sequencer.hpp"
#include "Reliability.hpp"

namespace Networking
{

class Connection
{
public:
    using Clock = std::chrono::steady_clock;

    Transport& m_Transport;
    asio::ip::udp::endpoint m_Endpoint;

    ReliabilityLayer m_Reliability;

    Clock::time_point m_LastMessageTime;

    Connection(Transport& transport, const asio::ip::udp::endpoint& endpoint, uint32_t sequence, uint32_t remoteSequence)
        : m_Transport(transport), m_Endpoint(endpoint)
    {
        auto& sequencer = m_Reliability.GetPacketSequencer();
        sequencer.SetSequence(sequence);
        sequencer.SetRemoteSequence(remoteSequence);
    }

    Connection(Transport& transport)
        : m_Transport(transport)
    {}

    ReliabilityLayer& GetReliabilityLayer() { return m_Reliability; }
    Clock::time_point GetLastSendTime() { return m_LastMessageTime; }

    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        return m_Reliability.HandleIncoming(remoteSequence, acknowledge, acknowledgeBits);
    }

    template<typename Handler>
    void Send(std::shared_ptr<Buffer> buffer, Handler&& handler)
    {
        m_Transport.Send(buffer, m_Endpoint,
        [this, callback = std::forward<Handle>(handler)](asio::error_code ec, std::size_t length))
        {
            callback(ec, length);
        });
    }
    
    void Send(std::shared_ptr<Buffer> buffer)
    {
        m_Transport.Send(buffer, m_Endpoint,[this](asio::error_code ec, std::size_t length)
        {
            m_LastMessageTime = Clock::now();
        });
    }

    void SendReliable(std::shared_ptr<Buffer> buffer)
    {
        auto result = m_Reliability.AddMessage(buffer);

        if(result)
        {
            Send(buffer, [this, sequence = result.value()](asio::error_code ec, std::size_t length)
            {
                m_Reliability.UpdateSendTime(sequence);
                m_LastMessageTime = Clock::now();
            });
        }
    }

    void Resend(std::chrono::steady_clock::time_point now)
    {
        using namespace std::chrono_literals;

        for(auto& [sequence, packet] : m_Reliability.GetPendingResends())
        {
            if(now - packet.lastSent >= 32ms)
            {
                Send(packet.packet, [this, sequence](asio::error_code ec, std::size_t length)
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
};

}