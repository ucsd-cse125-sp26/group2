/// @file InputSendSystem.hpp
/// @brief Serialises and sends the local player's InputSnapshot to the server.

#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"

/// @brief Client-only system that serialises and sends input to the server.
namespace systems
{

/// @brief Send the local player's current InputSnapshot over the network.
/// @param registry  The ECS registry.
/// @param conn      Network connection to the server.
inline void runInputSend(Registry& registry, Client& conn)
{
    registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) { conn.send(&snap, sizeof(snap)); });
}

} // namespace systems
