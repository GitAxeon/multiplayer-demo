#pragma once

#include <thread>
#include <unordered_map>
#include <vector>

#include "Listener.hpp"

#include "Slot.hpp"
#include "OptionalRef.hpp"
#include "NaiveDoubleBuffer.hpp"
#include "InternalEvent.hpp"

#include "Buffer.hpp"
#include "Serialization/Serialization.hpp"

#include "Packet.hpp"
#include "Reliability/Reliability.hpp"

namespace Networking
{

class NetworkLayer
{
public:
    NetworkLayer(NaiveDoubleBuffer<InternalEvent>& eventBuffer)
    :   m_IoContext(), m_WorkGuard(asio::make_work_guard(m_IoContext)),
        m_InternalEvents(eventBuffer)
    {
        m_NetworkThread = std::thread([this]()
        {
            std::println("Network thread started");

            m_IoContext.run();
            
            std::println("Network thread stopping");
        });

        // Reallocations will cause sockets to explode because they contain lamdas that capture 'this'
        // m_SocketTable.Resize(32);
        m_ListenerTable.Resize(32);
        m_ConnectionTable.Resize(32);
    }

    void Shutdown()
    {
        asio::post(m_IoContext, [this]()
        {       
            m_ListenerTable.ForEachActiveHandle([this](auto handle, Listener2& element)
            {
                CloseListenerInternal(handle);
                std::println("Closed listener [{} ,{}]", handle.Index(), handle.Generation());
                m_InternalEvents.Push(Event::ListenerClosed{handle, ListenerError::UserRequestedClose});
            });

            m_ListenerTable.Clear();

            m_ConnectionTable.ForEachActiveHandle([this](auto handle, Connection2& element)
            {
                CloseConnectionInternal(handle);
                std::println("Closed connection [{}, {}]", handle.Index(), handle.Generation());
                m_InternalEvents.Push(Event::ConnectionClosed{handle, ConnectionError::UserRequestedClose});
            });

            m_ConnectionTable.Clear();
        });

        m_WorkGuard.reset();
        m_IoContext.stop();

        if(m_NetworkThread.joinable())
            m_NetworkThread.join();
    }

    ListenerHandle CreateListener(asio::ip::port_type port)
    {
        ListenerHandle handle = m_ListenerTable.AllocateSlot();
        
        asio::post(m_IoContext, [this, handle, port]() mutable
        {
            CreateListenerInternal(handle, port);
        });

        return handle;
    }

    ConnectionHandle Connect(asio::ip::udp::endpoint endpoint)
    {
        ConnectionHandle handle = m_ConnectionTable.AllocateSlot();

        asio::post(m_IoContext, [this, handle, endpoint]() mutable
        {
            ConnectInternal(handle, endpoint); 
        });

        return handle;
    }

    void Send(ConnectionHandle handle, std::span<const std::byte> data, Reliability reliability)
    {
        ByteVector buffer(data.begin(), data.end());
        asio::post(m_IoContext, [this, handle, buffer = std::move(buffer)]() mutable
        {
            if(auto connection = m_ConnectionTable.Get(handle))
            {
                if(auto socket = m_SocketTable.Get(connection->m_Socket))
                {
                    socket->Send(buffer, connection->m_Remote);
                }
            }
        });
    }

    void CloseConnection(ConnectionHandle handle)
    {
        asio::post(m_IoContext, [this, handle]() mutable
        {
            CloseConnectionInternal(handle);
        });
    }
    
    void CloseListener(ListenerHandle handle)
    {
        asio::post(m_IoContext, [this, handle]() mutable
        {
            CloseListenerInternal(handle);
        });
    }

    void AcceptConnection(ConnectionHandle connection)
    {
        // Note: Maybe track internally which listener has the connection so 
        // a connection can be accepted/declined using ConnectionHandle only rather than ListenerHandle + ConnectionHandle
    }

    void RejectConnection(ConnectionHandle connection)
    {

    }

private:
    void CreateListenerInternal(ListenerHandle handle, asio::ip::port_type port)
    {
        auto socketHandle = m_SocketTable.AllocateSlot();
        auto socket = m_SocketTable.EmplaceInto(socketHandle, socketHandle, m_IoContext, SocketContext{m_SocketTable});

        if(!socketHandle || !socket)
        {
            std::println("Failed to allocate new socket");
        }

        const auto bindError = socket->Bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port));

        if(bindError != TransportError::None)
        {
            m_InternalEvents.Push(Event::ListenerClosed{handle, ListenerError::TransportError, bindError});
            return;
        }

        socket->SetReceiveCallback
        (
            [this, listenerHandle = handle](IncomingDatagram const& datagram) mutable
            {
                if(datagram.data.size() < sizeof(Packet::Header))
                {
                    std::println("Listener: Received datagram smaller than packet header. Datagram dropped.");
                    std::println("Listener: Packet size: {}, header size: {}", datagram.data.size(), sizeof(Packet::Header));
                    return;
                }

                Packet::Header header;
                Serialization::StreamReader reader(datagram.data);
                
                if(!reader.Read(header))
                {
                    std::println("Listener: Failed to parse packet header. Datagram dropped.");
                    return;
                }

                if(header.protocol != sProtocol2)
                {
                    std::println("Listener: Packet header protocol mismatch. Datagram dropped.");
                    return;
                }

                if(auto connection = m_ConnectionTable.Get(ConnectionHandle(header.connectionId, header.idGeneration)))
                {
                    // Check if connection is alive? Closing?

                    ByteVector buffer;

                    if(datagram.data.size() > sizeof(Packet::Header))
                    {
                        // The portion of data after the header
                        buffer.assign(datagram.data.begin() + sizeof(Packet::Header), datagram.data.end());
                    }
                    
                    m_InternalEvents.Push
                    (
                        Event::DataReceived
                        (
                            connection->m_Handle,
                            (!buffer.empty() ? std::move(buffer) : ByteVector{})
                        )
                    );
                }
                else if(auto listener = m_ListenerTable.Get(listenerHandle))
                {
                    auto result = listener->OnReceiveData(datagram);

                    if(result)
                    {
                        auto connectionHandle = m_ConnectionTable.AllocateSlot();

                        auto socketHandle = m_ListenerTable.Get(listenerHandle)->m_Socket;
                        m_ConnectionTable.EmplaceInto(connectionHandle, connectionHandle, socketHandle, datagram.endpoint, listenerHandle);

                        m_InternalEvents.Push
                        (
                            Event::IncomingConnectionAccepted
                            (
                                listenerHandle,
                                connectionHandle,
                                datagram.endpoint
                            )
                        );
                    }
                }
                else
                {
                    std::println("Listener: Datagram addressed to a handler that no longer exists. Datagram dropped.");
                }
            }
        );

        
        auto success = m_ListenerTable.EmplaceInto(handle, handle, socketHandle);
        if(success)
            socket->StartReceiving();

        m_InternalEvents.Push(Event::ListenerStarted(handle));
    }

    void ConnectInternal(ConnectionHandle handle, asio::ip::udp::endpoint endpoint)
    {
        auto socketHandle = m_SocketTable.AllocateSlot();
        auto socket = m_SocketTable.EmplaceInto(socketHandle, socketHandle, m_IoContext, SocketContext{m_SocketTable});

        const auto bindError = socket->Bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
        
        if(bindError != TransportError::None)
        {
            m_InternalEvents.Push(Event::ConnectionClosed{handle, ConnectionError::LocalClosed});
            return;
        }

        socket->SetReceiveCallback
        (
            [this, handle](IncomingDatagram const& datagram) mutable
            {
                if(auto connection = m_ConnectionTable.Get(handle))
                {
                    if(connection->m_State == ConnectionState::Connected)
                    {
                        ByteVector buffer;

                        if(datagram.data.size() > sizeof(Packet::Header))
                        {
                            // The portion of data after the header
                            buffer.assign(datagram.data.begin() + sizeof(Packet::Header), datagram.data.end());
                        }

                        std::println("Connection: Received {} bytes from {}", datagram.data.size(), datagram.endpoint);

                        // Push data received event?
                        m_InternalEvents.Push(Event::DataReceived{handle, std::move(buffer)});
                    }
                    else
                    {
                        // Reconnect? Drop connection? push event
                    }
                }
            }
        );

        auto success = m_ConnectionTable.EmplaceInto(handle, handle, socketHandle, endpoint);
        if(success)
            socket->StartReceiving();

        m_InternalEvents.Push(Event::OutgoingConnectionEstablished{handle});
    }

    void CloseListenerInternal(ListenerHandle handle)
    {
        if(auto listener = m_ListenerTable.Get(handle))
        {
            // Close related connections
            m_ConnectionTable.ForEachActiveHandle([this, handle](ConnectionHandle connectionHandle, Connection2& connection)
            {
                if(connection.m_Listener == handle)
                {
                    m_ConnectionTable.Reset(connectionHandle);
                }
            });

            if(auto socket = m_SocketTable.Get(listener->m_Socket))
            {
                socket->Close();
                m_SocketTable.Reset(listener->m_Socket);
            }

            m_ListenerTable.Reset(handle);
            m_InternalEvents.Push(Event::ListenerClosed{handle, ListenerError::UserRequestedClose});
        }
    }

    void CloseConnectionInternal(ConnectionHandle handle)
    {
        if(auto connection = m_ConnectionTable.Get(handle))
        {
            // For outgoing connections close the underlying socket
            if(connection->m_Listener == ListenerHandle::Invalid)
            {
                if(auto socket = m_SocketTable.Get(connection->m_Socket))
                {
                    socket->Close();
                    m_SocketTable.Reset(connection->m_Socket);
                }
            }

            m_ConnectionTable.Reset(handle);
            m_InternalEvents.Push(Event::ConnectionClosed{handle, ConnectionError::UserRequestedClose});
        }
    }

private:
    asio::io_context m_IoContext;
    asio::executor_work_guard<asio::io_context::executor_type> m_WorkGuard;
    std::thread m_NetworkThread;
    
    asd::SlotTable<AsyncSocket, SocketHandle> m_SocketTable;
    asd::SlotTable<Listener2, ListenerHandle> m_ListenerTable;
    asd::SlotTable<Connection2, ConnectionHandle> m_ConnectionTable;

    bool m_Open{true};

    NaiveDoubleBuffer<InternalEvent>& m_InternalEvents;
};

};