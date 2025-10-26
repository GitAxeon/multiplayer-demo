#pragma once

#include <cmath>
#include <random>

#include <concepts>

namespace Random
{
    thread_local std::mt19937 s_Generator(std::random_device{}());

    // Includes both 0.0f and 1.0f
    template<std::floating_point T>
    T RandomFloatNormalized()
    {
        static std::uniform_real_distribution<T> distribution
        (
            static_cast<T>(0.0),
            std::nextafter(static_cast<T>(1.0), std::numeric_limits<T>::max())
        );
        
        return distribution(s_Generator);
    }

    template<std::floating_point T>
    T RandomFloat()
    {
        constexpr auto min = std::numeric_limits<T>::min();
        constexpr auto max = std::numeric_limits<T>::max();
        
        std::uniform_real_distribution<T> distribution(min, max);

        return distribution(s_Generator);
    }

    template<std::floating_point T>
    T RandomFloat(T start, T end)
    {
        if(start == end)
            return start;

        if(start > end)
            std::swap(start, end);

        end = std::nextafter(end, std::numeric_limits<T>::max());
        
        std::uniform_real_distribution<T> distribution(start, end);

        return distribution(s_Generator);
    }

    template<std::integral T>
    T RandomInt()
    {
        constexpr auto min = std::numeric_limits<T>::min();
        constexpr auto max = std::numeric_limits<T>::max();
        static std::uniform_int_distribution<T> distribution(min, max);

        return distribution(s_Generator); 
    }

    template<std::integral T>
    T RandomInt(T start, T end)
    {
        if(start == end)
            return start;

        if(start > end)
            std::swap(start, end);
        
        std::uniform_int_distribution<T> distribution(start, end);

        return distribution(s_Generator); 
    }
}