#pragma once

#include <vector>

#include "Message.hpp"

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
    ClientId connectionId;
    std::vector<std::byte> data;
};

}