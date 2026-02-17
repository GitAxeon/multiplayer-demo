#pragma once

#include <mutex>
#include <vector>

namespace Networking
{

template<typename T>
class NaiveDoubleBuffer
{
public:

    void Reserve(std::size_t size)
    {
        m_WriteBuffer.reserve(size);
        m_ReadBuffer.reserve(size);
    }
    void Push(T&& value)
    {
        std::lock_guard lock(m_Mutex);
        m_WriteBuffer.push_back(std::move(value));
    }

    void Swap()
    {
        std::lock_guard lock(m_Mutex); 

        m_ReadBuffer.clear();
        m_WriteBuffer.swap(m_ReadBuffer);
    }

    std::vector<T>& ReadBuffer()
    {
        return m_ReadBuffer;
    }

    void Clear()
    {
        m_ReadBuffer.clear();
    }

private:
    std::vector<T> m_WriteBuffer;
    std::vector<T> m_ReadBuffer;
    std::mutex m_Mutex;
};

}