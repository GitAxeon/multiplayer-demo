#pragma once

#include <unordered_map>
#include <thread>

#include "AsyncSocket.hpp"

#include "Connection.hpp"
#include "Listener.hpp"

struct NullToken_t {};
constexpr NullToken_t NullOption; 

template<typename T>
class OptionalRef
{
public:
    OptionalRef() = default;
    OptionalRef(NullToken_t) {}
    OptionalRef(T& value) : m_Pointer(&value) {}

    operator bool() const { return m_Pointer; }

    // Will explode if you don't check that m_Pointer has value
    T* operator->() { return m_Pointer; }
    T& value() { return *m_Pointer; }

private:
    T* m_Pointer{nullptr};
};


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
    }

    ~NetworkSystem()
    {
        Shutdown();
    }

    void Shutdown()
    {
        m_WorkGuard.reset();
        m_Context.stop();

        m_NetworkThread.join();
    }

    void Update(std::chrono::milliseconds dt)
    {

    }

    // Server
    ListenerHandle Listen(asio::ip::port_type port)
    {
        ListenerHandle nextHandle(m_NextListenerId);

        if(auto it = m_Listeners.find(nextHandle); it != m_Listeners.end())
        {
            return ListenerHandle::Invalid;
            // return InvalidListenerHandle;
        }

        Listener2 listener(nextHandle, m_Context);

        if(!listener.socket->Bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port)))
        {
            return ListenerHandle::Invalid;
            // return InvalidListenerHandle;
        }

        listener.socket->SetReceiveCallback
        (
            [this, handle = nextHandle](const auto& datagram)
            {
                if(auto result = FindListener(handle))
                {   
                    result->HandleDatagram(datagram);
                }
                else
                {
                    std::println("Listener requested by removed handle. (Socket receive)");
                }
            }
        );

        listener.socket->ScheduleReceive();

        m_Listeners.emplace(nextHandle, std::move(listener));
        
        m_NextListenerId++;
        
        return nextHandle;
    }

    // Client
    ConnectionHandle Connect(const asio::ip::udp::endpoint& endpoint)
    {
         
    }

    void Send(ConnectionHandle id, std::span<std::byte> data, Reliability reliability = Reliability::Unreliable)
    {

    }

private:
    void HandleDatagram(const IncomingDatagram& datagram)
    {

    }

    OptionalRef<Listener2> FindListener(ListenerHandle handle)
    {
        auto find = m_Listeners.find(handle);
        
        if(find != m_Listeners.end())
        {
            return m_Listeners.at(handle);
        }

        return NullOption;
    }

private:
    asio::io_context m_Context;
    std::thread m_NetworkThread;
    asio::executor_work_guard<asio::io_context::executor_type> m_WorkGuard;

    std::unordered_map<ListenerHandle, Listener2> m_Listeners;
    std::unordered_map<ConnectionHandle, Connection2> m_Connections;

    std::uint32_t m_NextListenerId {1};
    ConnectionHandle m_NextConnectionId {1};
};

}