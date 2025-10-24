#pragma once

#include <memory>
#include <chrono>                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
#include <unordered_map>

#include "NetworkBuffer.hpp"
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

struct ReliabilityLayer
{
    // Return true if the message hasn't been acknowledged before
    bool HandleIncoming(uint32_t remoteSequence, uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        bool isNew = sequencer.RecordIncomingSequence(remoteSequence);
        
        if(!isNew)
            return false;

        UpdateResendBuffer(remoteSequence, acknowledgeBits);

        return true;
    }

    void UpdateResendBuffer(uint32_t acknowledge, uint32_t acknowledgeBits)
    {
        for(auto it = resendBuffer.begin(); it != resendBuffer.end();)
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
                it = resendBuffer.erase(it);
            }
            else
            {
                it++;
            }
        }
    }
    
    PacketSequencer sequencer;
    std::unordered_map<uint32_t, ReliableMessage> resendBuffer;
};

}