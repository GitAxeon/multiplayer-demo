#pragma once

#include <vector>
#include <string_view>

namespace Frame3
{

struct CommandlineArguments
{
    CommandlineArguments(const int argc, const char* const argv[])
    {
        for(int i = 0; i < argc; i++)
        {
            arguments.emplace_back(argv[i]);
        }
    };

    std::vector<std::string_view> arguments;
    std::size_t Count() const { return arguments.size(); }
};

}