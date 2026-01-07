#pragma once

#include <vector>
#include <string_view>
#include <optional>

namespace Frame3
{

struct CommandlineArguments
{
public:
    CommandlineArguments(const int argc, char const* const argv[])
    {
        for(int i = 0; i < argc; i++)
        {
            Values.emplace_back(argv[i]);
        }
    };

    std::size_t size() const { return Values.size(); }

    std::optional<std::string_view> get(std::size_t index) const
    {
        if(index < Values.size())
        {
            return Values[index];
        }

        return std::nullopt;
    }

public:
    std::vector<std::string_view> Values;
};

}