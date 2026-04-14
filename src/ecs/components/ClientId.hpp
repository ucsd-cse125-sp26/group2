/// @file ClientId.hpp
/// @brief Network client identifier component for multiplayer entities.

#pragma once

#include <functional>

/// @brief Associates an entity with a connected network client.
struct ClientId
{
    int value = -1; ///< Network client ID. -1 = unassigned.
};

/// @brief Equality operator for ClientId, compares the underlying value.
inline bool operator==(const ClientId& a, const ClientId& b) noexcept
{
    return a.value == b.value;
}

/// @brief Hash function for ClientId, allowing it to be used as a key in unordered containers.
template <>
struct std::hash<ClientId>
{
    std::size_t operator()(const ClientId& id) const noexcept { return std::hash<int>{}(id.value); }
};
