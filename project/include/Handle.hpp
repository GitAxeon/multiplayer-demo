#pragma once

#include <concepts>
#include <limits>
#include <functional>

namespace asd
{

template<typename Tag, std::unsigned_integral T = std::uint32_t>
class Handle
{
public:
    using ValueType = T;
    
    constexpr static T InvalidValue { std::numeric_limits<T>::max() };
    constexpr static T Min          { std::numeric_limits<T>::min() };
    constexpr static T Max          { std::numeric_limits<T>::max() - 1 };

    static const Handle Invalid; // Equals to Handle<Tag, T>{InvalidValue, InvalidValue}
    
    constexpr Handle() : m_Index(InvalidValue), m_Generation(InvalidValue) {}
    
    explicit constexpr Handle(T value, T generation) 
        : m_Index(value), m_Generation(generation) {}

    constexpr bool Valid() const { return m_Index != InvalidValue && m_Generation != InvalidValue; }
    constexpr T Index() const { return m_Index; }
    constexpr T Generation() const { return m_Generation; }

    explicit constexpr operator bool() const { return Valid(); }

    friend constexpr auto operator<=>(const Handle&, const Handle&) = default;

private:
    T m_Index;
    T m_Generation;
};

// Invalid requires out of class initialization because msvc considers the type incomplete ?
template<typename Tag, std::unsigned_integral T>
constexpr Handle<Tag, T> Handle<Tag, T>::Invalid{InvalidValue, InvalidValue};

}

namespace std
{

template<typename Tag>
struct hash<asd::Handle<Tag, std::uint32_t>>
{
    size_t operator()(const asd::Handle<Tag, std::uint32_t>& handle) const
    {
        return std::hash<std::uint64_t>{}
        (
            (static_cast<std::uint64_t>(handle.Index()) << 32) | handle.Generation()
        );
    }
};

}