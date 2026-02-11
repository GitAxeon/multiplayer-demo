#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace Networking
{

using ByteVector = std::vector<std::byte>;

template<std::size_t size>
using ByteArray = std::array<std::byte, size>;

}