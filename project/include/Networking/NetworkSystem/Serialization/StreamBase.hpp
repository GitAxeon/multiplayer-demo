#pragma once

#include <cstddef>

namespace Serialization
{

class StreamBase
{
public:
    std::size_t StreamPosition() const { return m_Position; }
    bool StreamPosition(std::size_t position)
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