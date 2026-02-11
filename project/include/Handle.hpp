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
    constexpr static T InvalidValue { std::numeric_limits<T>::max() };
    constexpr static T Min          { std::numeric_limits<T>::min() };
    constexpr static T Max          { std::numeric_limits<T>::max() - 1 };

    static const Handle Invalid; // Equals to Handle<Tag, T>{InvalidValue}
    
    constexpr Handle() : m_Value(InvalidValue) {}
    explicit constexpr Handle(T value) : m_Value(value) {}

    constexpr bool Valid() const { return m_Value != InvalidValue; }

    explicit constexpr operator T() const { return m_Value; }
    explicit constexpr operator bool() const { return Valid(); }

    friend constexpr auto operator<=>(const Handle&, const Handle&) = default;

private:
    T m_Value;
};

// Invalid requires out of class initialization because msvc considers the type incomplete ?
template<typename Tag, std::unsigned_integral T>
constexpr Handle<Tag, T> Handle<Tag, T>::Invalid{Handle<Tag, T>{InvalidValue}};

}

namespace std
{

template<typename Tag, std::unsigned_integral T>
struct hash<asd::Handle<Tag, T>>
{
    size_t operator()(const asd::Handle<Tag, T>& handle) const
    {
        return std::hash<T>{}(static_cast<T>(handle));
    }
};

}