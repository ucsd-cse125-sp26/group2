/// @file InputSampleSystem.hpp
/// @brief Client-only input sampling for mouse look and movement keys.

#pragma once

#include "ecs/components/Controllable.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/RespawnTimer.hpp"
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
/// @param gravityFlipped    When true, both mouse axes are inverted so
///                          controls feel natural while the camera is
///                          rolled 180° for upside-down gravity.
inline void runMouseLook(Registry& registry, float mouseSensitivity, bool gravityFlipped = false)
{
    float mdx = 0.0f;
    float mdy = 0.0f;
    SDL_GetRelativeMouseState(&mdx, &mdy);

    // When gravity is flipped the camera is rolled 180°, which swaps
    // both screen-left/right and screen-up/down relative to world space.
    // Negating both deltas keeps controls feeling natural.
    if (gravityFlipped) {
        mdx = -mdx;
        mdy = -mdy;
    }

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
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

/// @brief Tracks previous-frame key state for edge detection.
inline bool prevKillSelfKey = false;
/// @brief Tracks previous-frame G key state for gravity flip edge detection.
inline bool prevFlipGravityKey = false;
/// @brief Tracks previous-frame 3 key state for grenade cycle edge detection.
inline bool prevGrenadeKey = false;

/// @brief Sample keyboard state into the movement flags.
///
/// Should be called **once per physics tick group** when input is synced
/// with physics (the default) so movement calculations match the server.
/// Can also be called every iterate() when the sync toggle is off.
///
/// @param registry        The ECS registry.
/// @param gravityFlipped  When true, A/D are swapped so strafing feels
///                        correct while the camera is rolled 180°.
inline void runMovementKeys(Registry& registry, bool gravityFlipped = false)
{
    const bool* const kKeys = SDL_GetKeyboardState(nullptr);

    // Edge-detect K key: only fire killSelf on the rising edge (key-down),
    // not while held.  Prevents respawn → immediate re-death loop.
    const bool killKeyNow = kKeys[SDL_SCANCODE_K];
    const bool killEdge = killKeyNow && !prevKillSelfKey;
    prevKillSelfKey = killKeyNow;

    // Edge-detect G key: gravity flip is a toggle, fire on rising edge only.
    const bool flipKeyNow = kKeys[SDL_SCANCODE_G];
    const bool flipEdge = flipKeyNow && !prevFlipGravityKey;
    prevFlipGravityKey = flipKeyNow;

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        snap.forward = kKeys[SDL_SCANCODE_W];
        snap.back = kKeys[SDL_SCANCODE_S];
        // When gravity is flipped the camera is rolled 180°, which negates
        // the screen-space right vector.  Swapping A/D compensates so
        // pressing A still moves the player screen-left.
        snap.left = gravityFlipped ? kKeys[SDL_SCANCODE_D] : kKeys[SDL_SCANCODE_A];
        snap.right = gravityFlipped ? kKeys[SDL_SCANCODE_A] : kKeys[SDL_SCANCODE_D];
        snap.jump = kKeys[SDL_SCANCODE_SPACE];
        snap.crouch = kKeys[SDL_SCANCODE_LCTRL];
        snap.ability1 = kKeys[SDL_SCANCODE_LSHIFT];
        snap.ability2 = kKeys[SDL_SCANCODE_E];
        snap.killSelf = killEdge;
        snap.flipGravity = flipEdge;
        snap.skipRespawn = false; // Clear stale flag from previous death.
    });
}

/// @brief Sample skip-respawn input while the local player is dead.
///
/// Runs independently of Controllable — dead players can press Space to
/// skip the remaining respawn timer and respawn immediately.
///
/// @param registry  The ECS registry.
inline void runDeadInput(Registry& registry)
{
    const bool* const kKeys = SDL_GetKeyboardState(nullptr);

    registry.view<InputSnapshot, LocalPlayer, RespawnTimer>().each(
        [&](InputSnapshot& snap, const RespawnTimer& /*unused*/) { snap.skipRespawn = kKeys[SDL_SCANCODE_SPACE]; });
}

/// @brief Sample keyboard state into the weapon flags.
///
/// Should be called **once per physics tick group** when input is synced
/// with physics (the default) so movement calculations match the server.
/// Can also be called every iterate() when the sync toggle is off.
///
/// @param registry  The ECS registry.
inline void runWeaponKeys(Registry& registry)
{
    const bool* const kKeys = SDL_GetKeyboardState(nullptr);
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);

    // Edge-detect 3 key: grenade cycle is a toggle, fire on rising edge only.
    const bool grenadeKeyNow = kKeys[SDL_SCANCODE_3];
    const bool grenadeEdge = grenadeKeyNow && !prevGrenadeKey;
    prevGrenadeKey = grenadeKeyNow;

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        snap.shooting =
            (mouse & SDL_BUTTON_LMASK) != 0; // Apply bitmask to mouse input, true if left click is held down.
        snap.switchToPrimary = kKeys[SDL_SCANCODE_1];
        snap.switchToSecondary = kKeys[SDL_SCANCODE_2];
        snap.cycleGrenade = grenadeEdge;
        snap.reload = kKeys[SDL_SCANCODE_R];
        snap.pickup = kKeys[SDL_SCANCODE_F];
    });
}

/// @brief Legacy combined sampler — calls both runMouseLook and runMovementKeys.
/// @param registry          The ECS registry.
/// @param mouseSensitivity  Radians per pixel (default 0.001).
inline void runInputSample(Registry& registry, float mouseSensitivity = 0.001f)
{
    runMouseLook(registry, mouseSensitivity);
    runMovementKeys(registry);
    runWeaponKeys(registry);
}

// ─── Gamepad samplers ────────────────────────────────────────────────────────
//
// Mirror the keyboard/mouse pipeline so a controller can drive every action a
// PC player has access to.  SDL3's gamepad API gives us a standardised
// abstract layout (Xbox 360 / One, DualShock, Switch Pro, etc. all map onto
// the same buttons/axes) so a single mapping serves "all controllers behave
// the same way".  Tested on Xbox 360.
//
// Gamepad samplers OR their state into the InputSnapshot fields that the
// keyboard samplers already populated, so a player can use kbm and pad
// simultaneously without one stomping the other.  The look sampler ADDs to
// yaw/pitch the same way mouse delta does, so the two compose cleanly.

namespace gamepad
{

/// @brief Stick deadzone as a fraction of full deflection.
///
/// Xbox 360 sticks notoriously drift around centre, so anything below this
/// reads as zero.  0.15 is a conservative middle-ground that ignores drift
/// without introducing perceptible "stick travel before motion".
inline constexpr float k_stickDeadzone = 0.15f;

/// @brief Trigger threshold for treating an analog trigger as "pressed".
///
/// Triggers are 0..1 analog; we want a clear press semantic for fire / grapple
/// (which are boolean in InputSnapshot).  0.5 matches the Xbox UI convention.
inline constexpr float k_triggerThreshold = 0.5f;

/// @brief Convert SDL's int16 axis value to a deadzoned float in [-1, 1].
///
/// Applies a radial deadzone and rescales so the live range is still [-1, 1]
/// (otherwise full stick deflection would only feel like ~0.85 of the range).
inline float normaliseAxis(Sint16 raw)
{
    // SDL_Gamepad axes are int16; normalise to [-1, 1].  -32768 is one larger
    // in magnitude than 32767 — divide by 32767 and clamp to keep the range
    // symmetric (otherwise full-down on a stick reads as -1.0000305...).
    const float v = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
    if (std::fabs(v) < k_stickDeadzone)
        return 0.0f;
    // Rescale [deadzone, 1] → [0, 1] so the user gets the full output range.
    const float sign = v < 0.0f ? -1.0f : 1.0f;
    return sign * (std::fabs(v) - k_stickDeadzone) / (1.0f - k_stickDeadzone);
}

} // namespace gamepad

/// @brief Sample the right stick into yaw / pitch.
///
/// Must be called **every iterate() call** — same cadence as runMouseLook —
/// so camera rotation is smooth at any frame rate.  Unlike mouse delta
/// (which is integrated over the time between calls), the stick gives an
/// instantaneous angular velocity, so we multiply by frame delta time.
///
/// Adds to existing yaw/pitch (so simultaneous mouse + stick compose).
///
/// @param registry          The ECS registry.
/// @param gamepad           Open gamepad, or nullptr to no-op.
/// @param lookSensitivity   Radians per second at full stick deflection.
/// @param dt                Frame delta time in seconds.
/// @param gravityFlipped    When true, both axes are inverted for 180° camera roll.
inline void
runGamepadLook(Registry& registry, SDL_Gamepad* gamepad, float lookSensitivity, float dt, bool gravityFlipped = false)
{
    if (!gamepad)
        return;

    float rx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    float ry = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));

    if (rx == 0.0f && ry == 0.0f)
        return;

    if (gravityFlipped) {
        rx = -rx;
        ry = -ry;
    }

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        // Sign convention matches runMouseLook: stick-right (positive rx)
        // should look right, which means yaw decreases (see runMouseLook
        // comment for why the negation is correct).
        snap.yaw -= rx * lookSensitivity * dt;
        snap.yaw = std::remainder(snap.yaw, glm::radians(360.0f));

        // SDL gamepad Y axis is +down/-up (screen-coord convention) — same as
        // mouse mdy — so adding directly matches mouse-down = pitch+ behaviour.
        snap.pitch = std::clamp(snap.pitch + ry * lookSensitivity * dt, -glm::radians(89.0f), glm::radians(89.0f));
    });
}

/// @brief Sample gamepad buttons / left stick into the movement flags.
///
/// ORs with whatever the keyboard sampler set so kbm + pad coexist.  Should
/// run on the same cadence as runMovementKeys (once per physics tick group
/// when synced, otherwise every iterate).
///
/// Mapping:
///   Left stick     → forward / back / strafe
///   A (south)      → jump
///   B (east)       → crouch
///   L3 (LS click)  → sprint
///   LT             → grapple   (analog, threshold @ 0.5)
///
/// @param registry        The ECS registry.
/// @param gamepad          Open gamepad, or nullptr to no-op.
/// @param gravityFlipped   When true, left/right stick are swapped to match
///                         the 180° camera roll.
inline void runGamepadMovement(Registry& registry, SDL_Gamepad* gamepad, bool gravityFlipped = false)
{
    if (!gamepad)
        return;

    const float lx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    const float ly = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));

    // Movement booleans are derived from a stronger threshold than the deadzone
    // so a player resting their thumb on the stick doesn't drift-walk.  0.3 is
    // ~tip pressure on a 360 stick.
    constexpr float moveThresh = 0.3f;
    const bool padForward = ly < -moveThresh; // stick-up = -Y in SDL
    const bool padBack = ly > moveThresh;
    // Swap left/right when gravity is flipped (same reasoning as keyboard).
    const bool padLeft = gravityFlipped ? (lx > moveThresh) : (lx < -moveThresh);
    const bool padRight = gravityFlipped ? (lx < -moveThresh) : (lx > moveThresh);

    const bool padJump = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    const bool padCrouch = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
    const bool padSprint = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);

    const float lt = static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f;
    const bool padGrapple = lt >= gamepad::k_triggerThreshold;

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        snap.forward |= padForward;
        snap.back |= padBack;
        snap.left |= padLeft;
        snap.right |= padRight;
        snap.jump |= padJump;
        snap.crouch |= padCrouch;
        snap.sprint |= padSprint;
        snap.grapple |= padGrapple;
    });
}

/// @brief Sample gamepad buttons / right trigger into the weapon flags.
///
/// ORs with whatever the keyboard sampler set so kbm + pad coexist.
///
/// Mapping:
///   RT             → shoot     (analog, threshold @ 0.5)
///   X (west)       → reload
///   Y (north)      → pickup
///   LB             → switchToPrimary
///   D-pad up       → switchToPrimary  (alt)
///   RB             → switchToSecondary
///   D-pad down     → switchToSecondary (alt)
///
/// @param registry  The ECS registry.
/// @param gamepad   Open gamepad, or nullptr to no-op.
inline void runGamepadWeapon(Registry& registry, SDL_Gamepad* gamepad)
{
    if (!gamepad)
        return;

    const float rt = static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) / 32767.0f;
    const bool padShoot = rt >= gamepad::k_triggerThreshold;
    const bool padReload = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);
    const bool padPickup = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
    const bool padPrimary = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ||
                            SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    const bool padSecondary = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ||
                              SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        snap.shooting |= padShoot;
        snap.reload |= padReload;
        snap.pickup |= padPickup;
        snap.switchToPrimary |= padPrimary;
        snap.switchToSecondary |= padSecondary;
    });
}

} // namespace systems
