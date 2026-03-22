#pragma once

#include <cstdint>

namespace Networking
{

enum class Reliability : uint8_t
{
    Reliable,
    Unreliable
};

}