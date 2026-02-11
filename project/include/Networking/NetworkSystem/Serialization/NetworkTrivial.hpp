#pragma once

#include <cstddef>
#include <cstdint>
#include <concepts>

namespace Serialization
{

namespace detail
{

/* True if T appears in the pack of Ts*/

template<typename T, typename...Ts>
concept is_one_of = (std::is_same_v<T, Ts> || ...);

}

/* Considered trivial to serialize */

template<typename T>
concept NetworkTrivial = detail::is_one_of
<
    std::remove_cvref_t<T>,
    std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
    std::int8_t,  std::int16_t,  std::int32_t,  std::int64_t,
    float, double,
    std::byte
>;

class StreamWriter;
class StreamReader;

template<typename T>
concept NetworkSerializable =
    requires(StreamWriter& writer, const T& source, 
             StreamReader& reader, T& destination)
    {
        { Serialize(writer, source) } -> std::same_as<bool>;
        { Deserialize(reader, destination) }-> std::same_as<bool>;
    };

}