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
    asio::ip::udp::endpoint endpoint;
    
    PacketSequencer sequencer;
    ReliabilityLayer reliability;

    Clock::time_point lastMessageTime;

    Connection(Transport& transport, const asio::ip::udp::endpoint& endpoint)
        : m_Transport(transport), endpoint(endpoint)
    {}

    Connection(Transport& transport)
        : m_Transport(transport)
    {}

    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        return reliability.HandleIncoming(remoteSequence, acknowledge, acknowledgeBits);
    }

    void Send(std::shared_ptr<Buffer> buffer)
    {
        m_Transport.Send(buffer, endpoint);
        lastMessageTime = Clock::now();
    }

    void SendReliable(std::shared_ptr<Buffer> buffer)
    {
        reliability.resendBuffer.emplace
        (
            std::piecewise_construct,
            std::forward_as_tuple(sequencer.CurrentSequence()),
            std::forward_as_tuple(buffer, sequencer.CurrentSequence())
        );

        Send(buffer);
        reliability.resendBuffer.at(sequencer.CurrentSequence()).lastSent = Clock::now();
    }

    void Resend(std::chrono::steady_clock::time_point now)
    {
        using namespace std::chrono_literals;

        for(auto& [sequence, packet] : reliability.resendBuffer)
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