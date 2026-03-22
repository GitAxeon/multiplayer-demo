#pragma once

#include <variant>

namespace asd::Variant
{
    template<typename...Ts>
    struct Overloaded : Ts... { using Ts::operator()...; };

    template<typename...Ts>
    Overloaded(Ts...) -> Overloaded<Ts...>;

    template<typename Event, typename...Handlers>
    void DispatchStrict(Event&& event, Handlers&&... handlers)
    {
        std::visit
        (
            Overloaded
            {
                std::forward<Handlers>(handlers)...
            },
            std::forward<Event>(event)
        );
    }

    template<typename Event, typename...Handlers>
    void DispatchLenient(Event&& event, Handlers&&... handlers)
    {
        std::visit
        (
            Overloaded
            {
                [](auto&&) noexcept {}, // default handler
                std::forward<Handlers>(handlers)...
            },
            std::forward<Event>(event)
        );
    }
}