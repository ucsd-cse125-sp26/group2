#pragma once

/// @brief Serialises and sends the local player's InputSnapshot to the server (not yet implemented).
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"
namespace systems
{

inline void runInputSend(Registry& registry, Client& conn)
{
    registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) { conn.send(&snap, sizeof(snap)); });
}

} // namespace systems
