#pragma once

#include <mutex>
#include <vector>
#include <thread>
#include <unordered_map>

#include "AsyncSocket.hpp"
#include "Connection.hpp"
#include "Listener.hpp"
#include "NetworkEvent.hpp"

#include "OptionalRef.hpp"
#include "NaiveDoubleBuffer.hpp"
#include "InternalEvent.hpp"

namespace Networking
{

enum class Reliability : uint8_t
{
    Reliable,
    Unreliable
};

class NetworkSystem
{
public:
    NetworkSystem()
        : m_Context(), m_WorkGuard(asio::make_work_guard(m_Context))
    {
        m_NetworkThread = std::thread([this]()
        {
            std::println("Network thread started");
            m_Context.run();
            std::println("Network thread stopping");
        });

        m_EventBuffer.Reserve(256);
    }

    ~NetworkSystem()
    {
        Shutdown();
    }

    void Shutdown()
    {
        for(auto& [_, listener] : m_Listeners)
            listener.Close();
        
        for(auto& [_, connection] : m_Connections)
            connection.Close();

        m_WorkGuard.reset();
        m_Context.stop();

        if(m_NetworkThread.joinable())
            m_NetworkThread.join();
    }

    void Update(std::chrono::milliseconds dt)
    {
        m_EventBuffer.Swap();
    }

    // Server
    ListenerHandle Listen(asio::ip::port_type port)
    {
        ListenerHandle nextHandle(m_NextListenerId);

        auto socket = std::make_unique<AsyncSocket>(m_Context);

        if(!socket)
        {
            return ListenerHandle::Invalid;
        }

        if(!socket->Bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port)))
        {
            return ListenerHandle::Invalid;
        }

        socket->SetReceiveCallback
        (
            [this, handle = nextHandle](IncomingDatagram const& datagram)
            {
                HandleDatagramFromListener(datagram, handle);
            }
        );

        socket->ScheduleReceive();

        Listener2 listener(nextHandle, std::move(socket));

        m_Listeners.emplace(nextHandle, std::move(listener));
        
        m_NextListenerId++;
        
        return nextHandle;
    }

    // Client
    ConnectionHandle Connect(asio::ip::udp::endpoint const& endpoint)
    {
        ConnectionHandle nextHandle(m_NextConnectionId);

        auto socket = std::make_unique<AsyncSocket>(m_Context);

        if(!socket)
        {
            return ConnectionHandle::Invalid;
        }

        if(!socket->Bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0)))
        {
            return ConnectionHandle::Invalid;
        }

        socket->SetReceiveCallback
        (
            [this, handle = nextHandle](IncomingDatagram const& datagram)
            {
                HandleDatagramFromConnection(datagram, handle);
            }
        );

        auto transport = std::make_unique<ClientSideTransport>(std::move(socket));
        Connection2 connection(nextHandle, std::move(transport), endpoint);
        
        m_Connections.emplace(nextHandle, std::move(connection));
        m_NextConnectionId++;

        return ConnectionHandle::Invalid;
    }

    void Send(ConnectionHandle id, std::span<std::byte> data, Reliability reliability = Reliability::Unreliable)
    {
        auto it = m_Connections.find(id);
        if(it == m_Connections.end())
        {
            return;
        }

        it->second.Send(data);
    }

private:
    asd::OptionalRef<Listener2> FindListener(ListenerHandle handle)
    {
        auto find = m_Listeners.find(handle);
        
        if(find != m_Listeners.end())
        {
            return m_Listeners.at(handle);
        }

        return asd::NullOption;
    }

    asd::OptionalRef<Connection2> FindConnection(asio::ip::udp::endpoint endpoint)
    {
        auto find = m_EndpointToConnectionHandle.find(endpoint);

        if(find != m_EndpointToConnectionHandle.end())
        {
            return m_Connections.at(find->second);
        }

        return asd::NullOption;
    }

    asd::OptionalRef<Connection2> FindConnection(ConnectionHandle handle)
    {
        auto find = m_Connections.find(handle);

        if(find != m_Connections.end())
        {
            return find->second;
        }

        return asd::NullOption;
    }

    void HandleDatagramFromListener(IncomingDatagram const& datagram, ListenerHandle handle)
    {
        if(auto connection = FindConnection(datagram.endpoint))
        {
            // Push something to event queue
            NetworkEvent event
            {
                .type = NetworkEvent::Type::Message,
                .connectionHandle = m_EndpointToConnectionHandle.at(datagram.endpoint),
                .data = std::move(datagram.data)
            };
            
            m_EventBuffer.Push(std::move(event));

            return;
        }

        if(auto listener = FindListener(handle))
        {
            auto newConnection = listener->HandleDatagram(datagram);

            if(newConnection)
            {
                m_ConnectionMutex.lock();
                
                ConnectionHandle handle(m_NextConnectionId);
                m_NextConnectionId++;
                
                m_Connections.emplace(ConnectionHandle(m_NextConnectionId), std::move(*newConnection));
                m_ConnectionMutex.unlock();

                NetworkEvent event
                {
                    .type = NetworkEvent::Type::Connected,
                    .connectionHandle = handle
                };

                m_EventBuffer.Push(std::move(event));
            }

            return;
        }
        else
        {
            std::println("A listener handle requested which does not exist. (Socket receive)");
        }
    }

    void HandleDatagramFromConnection(IncomingDatagram const& datagram, ConnectionHandle handle)
    {
        if(auto connection = FindConnection(handle))
        {

        }
    }

private:
    asio::io_context m_Context;
    std::thread m_NetworkThread;
    asio::executor_work_guard<asio::io_context::executor_type> m_WorkGuard;

    std::unordered_map<ListenerHandle, Listener2> m_Listeners;

    std::mutex m_ConnectionMutex;
    std::unordered_map<ConnectionHandle, Connection2> m_Connections;
    std::unordered_map<asio::ip::udp::endpoint, ConnectionHandle> m_EndpointToConnectionHandle;

    std::uint32_t m_NextListenerId {0};
    std::uint32_t m_NextConnectionId {0};

    NaiveDoubleBuffer<InternalEvent> m_InternalEvents;
    NaiveDoubleBuffer<NetworkEvent> m_EventBuffer;
};

}