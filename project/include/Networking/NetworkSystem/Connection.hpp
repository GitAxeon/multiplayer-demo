#pragma once

#include <memory>

#include "AsyncSocket.hpp"
#include "Transport.hpp"

namespace Networking
{

using ConnectionHandle = std::uint32_t;
inline constexpr ConnectionHandle InvalidConnectionHandle = 0;

struct Connection2
{
    Connection2(ConnectionHandle handle, std::unique_ptr<ITransport> transport)
        : handle(handle), m_Transport(std::move(transport)) {}
    
    ConnectionHandle handle{InvalidConnectionHandle};
    std::unique_ptr<ITransport> m_Transport{nullptr};
};

}