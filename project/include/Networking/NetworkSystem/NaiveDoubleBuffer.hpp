#pragma once

#include <mutex>
#include <vector>
#include <span>

namespace Networking
{

template<typename T>
class NaiveDoubleBuffer
{
public:
    void Reserve(std::size_t size)
    {
        m_BackBuffer.reserve(size);
        m_FrontBuffer.reserve(size);
    }

    template<typename...Args>
    void Emplace(Args&&... args)
    {
        std::lock_guard lock(m_Mutex);
        m_BackBuffer.emplace_back(std::forward<Args>(args)...);
    }

    void Push(T const& value)
    {
        std::lock_guard lock(m_Mutex);
        m_BackBuffer.push_back(value);
    }

    void Push(T&& value)
    {
        std::lock_guard lock(m_Mutex);
        m_BackBuffer.push_back(std::move(value));
    }

    void Swap()
    {
        std::lock_guard lock(m_Mutex); 
        m_FrontBuffer.clear();
        m_FrontBuffer.swap(m_BackBuffer);
    }

    std::span<const T> Read() const
    {
        return m_FrontBuffer;
    }

    void Clear()
    {
        m_FrontBuffer.clear();
    }

    bool Empty() const
    {
        return m_FrontBuffer.empty();
    }

private:
    std::vector<T> m_BackBuffer;
    std::vector<T> m_FrontBuffer;
    std::mutex m_Mutex;
};

}