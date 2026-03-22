#pragma once

#include <mutex>
#include <vector>
#include <thread>
#include <unordered_map>
#include <set>

#include "AsyncSocket.hpp"
#include "Connection.hpp"
#include "Listener.hpp"
#include "NetworkEvent.hpp"

#include "OptionalRef.hpp"
#include "NaiveDoubleBuffer.hpp"
#include "InternalEvent.hpp"

#include "Packet.hpp"

#include "NetworkLayer.hpp"

namespace Networking
{


class NetworkSystem
{
public:
    NetworkSystem() : m_NetworkLayer(m_NetworkEvents)
    {
        m_NetworkEvents.Reserve(256);
    }

    ~NetworkSystem()
    {
        Shutdown();
    }

    void Shutdown()
    {
        m_NetworkLayer.Shutdown();

        m_NetworkEvents.Clear();

        m_ActiveConnections.clear(),
        m_ActiveListeners.clear();
    }

    void Update()
    {
        m_NetworkEvents.Swap();

    }

    std::span<const InternalEvent> Events() const
    {
        return m_NetworkEvents.Read();
    }

    // Server
    ListenerHandle Listen(asio::ip::port_type port)
    {
        const auto handle = m_NetworkLayer.CreateListener(port);
        
        m_ActiveListeners.insert(handle);

        return handle;
    }

    void AcceptConnection(ConnectionHandle handle)
    {
        m_NetworkLayer.AcceptConnection(handle);
    }

    void RejectConnection(ConnectionHandle handle)
    {
        m_NetworkLayer.RejectConnection(handle);
    }

    // Client
    ConnectionHandle Connect(std::string_view address, asio::ip::port_type port)
    {
        const auto endpoint = asio::ip::udp::endpoint(asio::ip::make_address(address), port);
        const auto handle = m_NetworkLayer.Connect(endpoint);

        m_ActiveConnections.insert(handle);

        return handle;
    }

    void CloseConnection(ConnectionHandle handle)
    {
        m_NetworkLayer.CloseConnection(handle);
    }

    void Send(ConnectionHandle id, std::span<std::byte> data, Reliability reliability = Reliability::Unreliable)
    {
        m_NetworkLayer.Send(id, data, reliability);
    }

private:
    std::set<ConnectionHandle> m_ActiveConnections;
    std::set<ListenerHandle> m_ActiveListeners;

    NaiveDoubleBuffer<InternalEvent> m_NetworkEvents;

    NetworkLayer m_NetworkLayer;
};

}