#pragma once

#include "Types.hpp"
#include "AsyncSocket.hpp"

namespace Networking
{

class SocketContext
{
public:
    explicit SocketContext(SocketTable& socketTable) : m_SocketTable(socketTable) {}
    
    auto Get(SocketHandle handle) { return m_SocketTable.Get(handle); }

private:
    SocketTable& m_SocketTable;
};

}