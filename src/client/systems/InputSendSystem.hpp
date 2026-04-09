#pragma once

/// @brief Serialises and sends the local player's InputSnapshot to the server (not yet implemented).
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"
#include "network/InputPacket.hpp"
namespace systems
{

inline void runInputSet(Registry& registry, Client& conn)
{
    registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) {
        InputPacket pkt;
        pkt.type = 0x01;
        pkt.forward = snap.forward;
        pkt.back = snap.back;
        pkt.left = snap.left;
        pkt.right = snap.right;
        pkt.jump = snap.jump;
        pkt.crouch = snap.crouch;
        pkt.yaw = snap.yaw;
        pkt.pitch = snap.pitch;
        pkt.roll = snap.roll;

        conn.send(&pkt, sizeof(pkt));
    });
}

} // namespace systems
