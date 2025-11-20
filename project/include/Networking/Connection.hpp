#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

#include <asio.hpp>

#include "Transport.hpp"
#include "Sequencer.hpp"
#include "Reliability.hpp"

namespace Networking
{

class Connection : public std::enable_shared_from_this<Connection>
{
public:
    using Clock = std::chrono::steady_clock;

    Connection(Transport& transport, const asio::ip::udp::endpoint& endpoint, uint32_t initialSequence, uint32_t initialRemoteSequence)
        : m_Transport(transport), m_Endpoint(endpoint)
    {
        auto& sequencer = m_Reliability.GetPacketSequencer();
        sequencer.SetSequence(initialSequence);
        sequencer.SetRemoteSequence(initialRemoteSequence);
    }

    Connection(Transport& transport)
        : m_Transport(transport)
    {}

    // No copy
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Movable
    Connection(Connection&&) = default;
    Connection& operator=(Connection&&) = default;

    ReliabilityLayer& GetReliabilityLayer() { return m_Reliability; }
    Clock::time_point GetLastSendTime() { return m_LastMessageTime; }

    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        return m_Reliability.HandleIncoming(remoteSequence, acknowledge, acknowledgeBits);
    }

    template<typename Handler>
    void Send(std::shared_ptr<Buffer> buffer, Handler&& handler)
    {
        m_Transport.get().Send(buffer, m_Endpoint,
        [this, callback = std::forward<Handler>(handler)](asio::error_code ec, std::size_t length) mutable
        {
            if(!ec) m_LastMessageTime = Clock::now();

            callback(ec, length);
        });
    }
    
    void Send(std::shared_ptr<Buffer> buffer)
    {
        m_Transport.get().Send(buffer, m_Endpoint,[this](asio::error_code ec, auto)
        {
            if(!ec) m_LastMessageTime = Clock::now();
        });
    }

    template<typename Handler>
    void SendReliable(std::shared_ptr<Buffer> buffer, Handler&& handler)
    {
        auto result = m_Reliability.AddMessage(buffer);

        if(result)
        {
            Send(buffer,
                [this, callback = std::forward<Handler>(handler), sequence = result.value()]
                (asio::error_code ec, std::size_t length) mutable
            {
                m_Reliability.UpdateSendTime(sequence);
                m_LastMessageTime = Clock::now();
                callback(ec, length);
            });
        }
    }

    void SendReliable(std::shared_ptr<Buffer> buffer)
    {
        auto result = m_Reliability.AddMessage(buffer);

        if(!result) return;
        
        Send(buffer, [this, sequence = result.value()](asio::error_code ec, std::size_t length)
        {
            if(ec)
                return;

            m_Reliability.UpdateSendTime(sequence);
            m_LastMessageTime = Clock::now();
        });
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
private:
    std::reference_wrapper<Transport> m_Transport;
    asio::ip::udp::endpoint m_Endpoint;

    ReliabilityLayer m_Reliability;

    Clock::time_point m_LastMessageTime;
};

}