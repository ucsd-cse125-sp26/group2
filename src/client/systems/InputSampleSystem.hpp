#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <glm/trigonometric.hpp>

/// @brief Client-only input sampling system — split into two halves so mouse
///        look can run every iterate() (smooth camera at any FPS) while
///        movement keys run once per physics tick group (server-consistent).
namespace systems
{

/// @brief Sample mouse delta and accumulate into yaw / pitch.
///
/// Must be called **every iterate() call** regardless of whether a physics
/// tick fires — this keeps camera rotation smooth at the render frame rate.
/// SDL_GetRelativeMouseState returns the accumulated delta since the
/// previous call, so the total rotation over any time window is identical
/// regardless of call frequency.
///
/// @param registry          The ECS registry.
/// @param mouseSensitivity  Radians per pixel of mouse movement.
inline void runMouseLook(Registry& registry, float mouseSensitivity)
{
    float mdx = 0.0f;
    float mdy = 0.0f;
    SDL_GetRelativeMouseState(&mdx, &mdy);

    registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) {
        // Negate: SDL mdx is positive when moving right, but positive yaw
        // rotates toward +X which maps to screen-left via glm::lookAt's
        // cross(forward, up) convention.  Negating gives the standard
        // "mouse right = look right" behaviour.
        snap.yaw -= mdx * mouseSensitivity;

        // Wrap yaw to [-π, π] to avoid float precision drift over time.
        snap.yaw = std::remainder(snap.yaw, glm::radians(360.0f));

        // Clamp pitch to avoid gimbal-lock at the poles.
        snap.pitch = std::clamp(snap.pitch + mdy * mouseSensitivity, -glm::radians(89.0f), glm::radians(89.0f));
    });
}

/// @brief Sample keyboard state into the movement flags.
///
/// Should be called **once per physics tick group** when input is synced
/// with physics (the default) so movement calculations match the server.
/// Can also be called every iterate() when the sync toggle is off.
///
/// @param registry  The ECS registry.
inline void runMovementKeys(Registry& registry)
{
    const bool* const k_keys = SDL_GetKeyboardState(nullptr);

    registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) {
        snap.forward = k_keys[SDL_SCANCODE_W];
        snap.back = k_keys[SDL_SCANCODE_S];
        snap.left = k_keys[SDL_SCANCODE_A];
        snap.right = k_keys[SDL_SCANCODE_D];
        snap.jump = k_keys[SDL_SCANCODE_SPACE];
        snap.crouch = k_keys[SDL_SCANCODE_LCTRL];
    });
}

/// @brief Legacy combined sampler — calls both runMouseLook and runMovementKeys.
/// @param registry          The ECS registry.
/// @param mouseSensitivity  Radians per pixel (default 0.001).
inline void runInputSample(Registry& registry, float mouseSensitivity = 0.001f)
{
    runMouseLook(registry, mouseSensitivity);
    runMovementKeys(registry);
}

} // namespace systems
