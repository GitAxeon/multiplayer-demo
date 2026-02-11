#pragma once

#include <bit>
#include <algorithm>

namespace Networking
{

inline constexpr std::endian NetworkEndian = std::endian::little;

template<typename T>
T SwapEndian(T value)
{
    if constexpr (sizeof(T) > 1)
    {
        auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
        std::ranges::reverse(bytes);
        return std::bit_cast<T>(bytes);
    }
}

}