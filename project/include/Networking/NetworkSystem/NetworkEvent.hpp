#pragma once

#include <vector>

#include "Connection.hpp"

using ClientId = std::uint32_t;

namespace Networking
{

struct NetworkEvent
{
    enum class Type
    {
        Connected,
        Disconnected,
        Message
    };

    Type type;
    ConnectionHandle connectionHandle;
    std::vector<std::byte> data;
};

}