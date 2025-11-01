#pragma once

#include <format>

#include <asio/ip/udp.hpp>

template<>
struct std::formatter<asio::ip::udp::endpoint> : std::formatter<std::string_view>
{
    auto format(const asio::ip::udp::endpoint& endpoint, std::format_context& ctx) const
    {
        auto tmp = std::format("{}:{}", endpoint.address().to_string(), endpoint.port());
        return std::formatter<std::string_view>::format(tmp, ctx);
    }
};