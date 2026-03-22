#pragma once

#include <atomic>
#include <concepts>
#include <vector>
#include <mutex>
#include <optional>
#include <functional>

#include "OptionalRef.hpp"

namespace asd
{

/*
    Slot uses optional<T> for storage
*/

template<typename T, std::integral IndexType = std::uint32_t>
class Slot
{
public:
    template<typename...Args>
    asd::OptionalRef<T> Emplace(Args&& ... args)
    {
        // todo: error checking
        m_Value.emplace(std::forward<Args>(args)...);
        return *m_Value;
    }

    void Reset()
    {
        m_Value.reset();
        m_Generation++;
    }

    T& Value() { return *m_Value; }
    T const& Value() const { return *m_Value; }

    bool Active() const { return m_Value.has_value(); }
    IndexType Generation() const { return m_Generation.load(); }

    explicit operator bool() const { return Active(); }
private:
    std::atomic<IndexType> m_Generation{0};
    std::optional<T> m_Value {std::nullopt};
};

/* Reserving slots ahead of time prevents bugs caused by reallocating memory (invalidates pointers acquired by calling Get(...) ) */
/* Todo: ^^^^^^^^^^^^^^^^^^*/

template<typename T, typename HandleType>
class SlotTable
{
public:
    using SlotType = Slot<T, typename HandleType::ValueType>;

    SlotTable() : m_Slots{32} {}
    ~SlotTable() { Clear(); }
    
    /* Will invalidate pointers and references use with care*/
    void Resize(std::size_t size)
    {
        m_Slots.resize(size);
    }

    /* Simply clear the underlying vectors, could cause problems in the future*/
    void Clear()
    {
        m_Slots.clear();
        m_Free.clear();
    }

    HandleType AllocateSlot()
    {
        std::lock_guard lock(m_Mutex);
        
        if(!m_Free.empty())
        {
            auto index = m_Free.back();
            m_Free.pop_back();

            return HandleType{index, m_Slots[index].Generation()};
        }

        if(m_NextIndex > m_Slots.size())
        {
            return HandleType::InvalidHandle;
        }

        auto index = m_NextIndex++;
        return HandleType{index, 0};
    };

    template<typename...Args>
    HandleType Emplace(Args&&...args)
    {
        const auto handle = AllocateSlot();
        
        auto& slot = m_Slots[handle.Index()];
        slot.Emplace(std::forward<Args>(args)...);

        return handle; 
    }

    template<typename...Args>
    asd::OptionalRef<T> EmplaceInto(HandleType handle, Args&&...args)
    {
        if(!ValidateHandle(handle))
            return asd::NullOption;

        auto& slot = m_Slots[handle.Index()];
        
        if(slot.Active())
            return asd::NullOption;

        return slot.Emplace(std::forward<Args>(args)...);
    }

    void Reset(HandleType handle)
    {
        if(!ValidateHandle(handle))
            return;

        auto& slot = m_Slots[handle.Index()];
        slot.Reset();

        {
            std::lock_guard lock(m_Mutex);
            m_Free.push_back(handle.Index());
        }
    }

    asd::OptionalRef<T> Get(HandleType handle)
    {
        if(!ValidateHandle(handle))
            return asd::NullOption;
        
        auto& slot = m_Slots[handle.Index()];

        if(!slot.Active())
            return asd::NullOption;
        
        return slot.Value();
    }

    template<typename Func>
    void ForEachActive(Func&& func)
    {
        for(auto& slot : m_Slots)
        {
            if(slot.Active())
                func(slot);
        }
    }

    template<typename Func>
    void ForEachActiveHandle(Func&& func)
    {
        for(std::size_t i = 0; i < m_Slots.size(); i++)
        {
            auto& slot = m_Slots[i];
            HandleType handle(i, slot.Generation());
            
            if(slot.Active())
                func(handle, slot.Value());
        }
    }

private:
    bool ValidateHandle(HandleType handle) const
    {
        if(!handle.Valid())
            return false;

        if(handle.Index() >= m_Slots.size())
            return false;
        
        if(handle.Generation() != m_Slots[handle.Index()].Generation())
            return false;

        // -
        return true;
    }

private:
    std::vector<SlotType> m_Slots;
    
    std::vector<std::size_t> m_Free;
    std::mutex m_Mutex;

    std::size_t m_NextIndex{0};
};

}