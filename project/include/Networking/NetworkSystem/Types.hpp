#pragma once

#include "Handle.hpp"
#include "Slot.hpp"

namespace Networking
{

using SocketHandle = asd::Handle<struct SocketTag>;
using ListenerHandle = asd::Handle<struct ListenerTag>;
using ConnectionHandle = asd::Handle<struct ConnectionTag>;

class AsyncSocket;
struct Listener2;
struct Connection2;

using SocketTable = asd::SlotTable<AsyncSocket, SocketHandle> ;
using ListenerTable = asd::SlotTable<Listener2, ListenerHandle>;
using ConnectionTable = asd::SlotTable<Connection2, ConnectionHandle>;

}