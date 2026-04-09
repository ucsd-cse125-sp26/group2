#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <glm/trigonometric.hpp>

/// @brief Client-only input sampling system.
///
/// Reads SDL keyboard and mouse state each frame and writes the result into
/// the InputSnapshot component of the LocalPlayer entity.
///
/// @note Must be called **once per frame**, before the physics accumulator loop,
///       so the snapshot is fresh for every physics tick that fires that frame.
namespace systems
{

/// @brief Sample keyboard and mouse state into the local player's InputSnapshot.
/// @param registry          The ECS registry.
/// @param mouseSensitivity  Mouse sensitivity in radians per pixel (default 0.002).
inline void runInputSample(Registry& registry, float mouseSensitivity = 0.002f)
{
    const bool* const k_keys = SDL_GetKeyboardState(nullptr);

    float mdx = 0.0f;
    float mdy = 0.0f;
    SDL_GetRelativeMouseState(&mdx, &mdy);

    registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) {
        snap.forward = k_keys[SDL_SCANCODE_W];
        snap.back = k_keys[SDL_SCANCODE_S];
        snap.left = k_keys[SDL_SCANCODE_A];
        snap.right = k_keys[SDL_SCANCODE_D];
        snap.jump = k_keys[SDL_SCANCODE_SPACE];
        snap.crouch = k_keys[SDL_SCANCODE_LCTRL];

        // Accumulate mouse deltas into absolute yaw.
        snap.yaw += mdx * mouseSensitivity;

        // Wrap yaw to [-π, π] to avoid float precision drift over time.
        snap.yaw = std::remainder(snap.yaw, glm::radians(360.0f));

        // Clamp pitch to avoid gimbal-lock at the poles.
        snap.pitch = std::clamp(snap.pitch + mdy * mouseSensitivity, -glm::radians(89.0f), glm::radians(89.0f));
    });
}

} // namespace systems
