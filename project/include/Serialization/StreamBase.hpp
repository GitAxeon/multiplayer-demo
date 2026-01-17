#pragma once

#include <cstddef>
#include <cstdint>

namespace Networking
{

class StreamBase
{
public:
    std::size_t GetStreamPosition() const { return m_Position; }
    bool SetStreamPosition(std::size_t position)
    { 
        m_Position = position;
        return true;
    }

    bool Ok() const { return m_Ok; }

protected:
    std::size_t m_Position{0};
    bool m_Ok{true};
};

}
