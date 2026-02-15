#pragma once

namespace asd
{
    
struct NullToken_t {};
constexpr NullToken_t NullOption; 

template<typename T>
class OptionalRef
{
public:
    OptionalRef() = default;
    OptionalRef(NullToken_t) {}
    OptionalRef(T& value) : m_Pointer(&value) {}

    operator bool() const { return m_Pointer; }

    // Will explode if you don't check that m_Pointer has value
    T* operator->() { return m_Pointer; }
    T& value() { return *m_Pointer; }

private:
    T* m_Pointer{nullptr};
};

}