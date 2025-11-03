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

    Connection(Transport& transport, const asio::ip::udp::endpoint& endpoint)
        : m_Transport(transport), m_Endpoint(endpoint)
    {}

    Connection(Transport& transport)
        : m_Transport(transport)
    {}

    ReliabilityLayer& GetReliabilityLayer() { return m_Reliability; }
    Clock::time_point GetLastSendTime() { return m_LastMessageTime; }

    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        return m_Reliability.HandleIncoming(remoteSequence, acknowledge, acknowledgeBits);
    }

    void Send(std::shared_ptr<Buffer> buffer)
    {
        m_Transport.Send(buffer, m_Endpoint);
        m_LastMessageTime = Clock::now();
    }

    void SendReliable(std::shared_ptr<Buffer> buffer)
    {
        m_Reliability.resendBuffer.emplace
        (
            std::piecewise_construct,
            std::forward_as_tuple(m_Reliability.GetPacketSequencer().CurrentSequence()),
            std::forward_as_tuple(buffer, m_Sequencer.CurrentSequence())
        );

        Send(buffer);
        m_Reliability.resendBuffer.at(m_Sequencer.CurrentSequence()).lastSent = Clock::now();
    }

    void Resend(std::chrono::steady_clock::time_point now)
    {
        using namespace std::chrono_literals;

        for(auto& [sequence, packet] : m_Reliability.resendBuffer)
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

}