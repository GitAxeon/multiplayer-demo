#pragma once

#include <cstddef>
#include <memory>
#include <bitset>

namespace asd
{

template<std::size_t N>
struct BitPool
{
    using IndexType = std::uint64_t;
    
    std::uint64_t FindFree()
    {
        for(std::size_t i = 0; i < N; i++)
        {
            std::uint64_t mask = ~active[i];
        }
    };

    std::uint64_t active[N];
};

class SlotTable
{
public:

private:
};

}