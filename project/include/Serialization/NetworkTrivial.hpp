#pragma once

#include <cstddef>
#include <cstdint>

namespace Networking
{

template<typename T> struct network_trivial : std::false_type {};

template<> struct network_trivial<std::byte> : std::true_type {};

// Enable if needed - Use for text only, no numerical values
// template<> struct NetworkTrivial<char> : std::true_type {};

// Will use std::uint8_t for storage since the data isn't bit packed
template<> struct network_trivial<bool> : std::true_type {};

// Note: Possibly need to check for validity
template<> struct network_trivial<float> : std::true_type {};
template<> struct network_trivial<double> : std::true_type {};

template<> struct network_trivial<std::uint8_t> : std::true_type {};
template<> struct network_trivial<std::uint16_t> : std::true_type {};
template<> struct network_trivial<std::uint32_t> : std::true_type {};
template<> struct network_trivial<std::uint64_t> : std::true_type {};

template<> struct network_trivial<std::int8_t> : std::true_type {};
template<> struct network_trivial<std::int16_t> : std::true_type {};
template<> struct network_trivial<std::int32_t> : std::true_type {};
template<> struct network_trivial<std::int64_t> : std::true_type {};

template<typename T> constexpr bool network_trivial_v = network_trivial<T>::value;
template<typename T> concept NetworkTrivialType = network_trivial_v<T>;

}