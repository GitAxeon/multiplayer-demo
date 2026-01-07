#pragma once 

#include <cstdint>

namespace Networking
{

class PacketSequencer
{
public:

    uint32_t LocalSequence() const
    {
        return m_SequenceNumber;
    }
    
    uint32_t RemoteSequence() const
    {
        return m_RemoteSequenceNumber;
    }

    uint32_t AcknowledgeBits() const 
    {
        return m_AcknowledgeBits;
    }
    
    void SetSequence(uint32_t value)
    {
        m_SequenceNumber = value;
    }

    void SetRemoteSequence(uint32_t value)
    {
        m_RemoteSequenceNumber = value;
    }

    uint32_t ObtainNewSequence()
    {
        return m_SequenceNumber++;
    }

    // Returns true if packet is new ie. not a duplicate
    bool RecordIncomingSequence(uint32_t sequenceNumber)
    {
        if(IsSequenceNewer(sequenceNumber, m_RemoteSequenceNumber))
        {        
            uint32_t diff = sequenceNumber - m_RemoteSequenceNumber;
            
            if(diff < 32)
            {
                m_AcknowledgeBits <<= diff;
            }
            else
            {
                m_AcknowledgeBits = 0;
            }

            m_AcknowledgeBits |= 1;
            m_RemoteSequenceNumber = sequenceNumber;

            return true;
        }
        else
        {
            uint32_t diff = m_RemoteSequenceNumber - sequenceNumber;

            if(diff >= 32)
            {
                return false;
            }

            uint32_t mask = 1u << diff;

            if(m_AcknowledgeBits & mask)
            {
                return false;
            }

            // m_AcknowledgeBits |= (1u << (diff - 1));
            m_AcknowledgeBits |= mask;
            return true;
        }
    }

    static bool IsSequenceNewer(uint32_t lhs, uint32_t rhs)
    {
        return static_cast<int32_t>(lhs - rhs) > 0;
    }

private:
    uint32_t m_SequenceNumber = 0;
    uint32_t m_RemoteSequenceNumber = 0;
    uint32_t m_AcknowledgeBits = 0;
};

}