#pragma once

#include <memory>
#include <chrono>                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
#include <unordered_map>
#include <optional>

#include "Buffer.hpp"
#include "Sequencer.hpp"

namespace Networking
{

struct ReliableMessage
{
    std::shared_ptr<Buffer> packet;
    uint32_t sequence = 0;
    std::chrono::steady_clock::time_point lastSent;

    int retries = 0;
    int m_MaxRetries = 32;
};

class ReliabilityLayer
{
public:
    std::size_t PendingReliableMessageCount() const
    {
        return m_ResendBuffer.size();
    }

    PacketSequencer& GetPacketSequencer() { return m_Sequencer; }
    const PacketSequencer& GetPacketSequencer() const { return m_Sequencer; }
    
    // Return sequence of the message if added succesfully
    std::optional<uint32_t> AddMessage(std::shared_ptr<Buffer> buffer)
    {
        const auto sequence = m_Sequencer.LocalSequence();
        auto [it, inserted] = m_ResendBuffer.try_emplace
        (
            sequence, // Key
            buffer, sequence
        );

        if(inserted)
        {
            return sequence;
        }

        return std::nullopt;
    }

    const std::unordered_map<uint32_t, ReliableMessage>& GetPendingResends() const
    {
        return m_ResendBuffer;
    }

    void UpdateSendTime(uint32_t sequence)
    {
        if(auto it = m_ResendBuffer.find(sequence); it != m_ResendBuffer.end())
        {
            it->second.lastSent = std::chrono::steady_clock::now();
        }
    }

    void IncrementResendCount(uint32_t sequence)
    {
        if(auto it = m_ResendBuffer.find(sequence); it != m_ResendBuffer.end())
        {
            it->second.retries++;
        }
    }

    // Return true if the message hasn't been acknowledged before
    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        bool isNew = m_Sequencer.RecordIncomingSequence(remoteSequence);
        
        if(!isNew) { return false; }

        UpdateResendBuffer(remoteSequence, acknowledgeBits);

        return true;
    }

    void UpdateResendBuffer(uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        for(auto it = m_ResendBuffer.begin(); it != m_ResendBuffer.end();)
        {
            uint32_t sequence = it->first;

            bool acknowledged = false;

            if(sequence == acknowledge)
            {
                acknowledged = true;
            }
            else if(PacketSequencer::IsSequenceNewer(acknowledge, sequence))
            {
                uint32_t diff = acknowledge - sequence;

                if(diff <= 32 && (acknowledgeBits & (1u << (diff - 1))) )
                {
                    acknowledged = true;
                }
            }

            if(acknowledged)
            {
                it = m_ResendBuffer.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

private:
    PacketSequencer m_Sequencer;
    std::unordered_map<uint32_t, ReliableMessage> m_ResendBuffer;
};

}