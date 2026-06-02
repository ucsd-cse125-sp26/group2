/// @file InputSampleSystem.hpp
/// @brief Client-only input sampling for mouse look and movement keys.

#pragma once

#include "config/InputBindings.hpp"
#include "config/UserSettings.hpp"
#include "ecs/components/Controllable.hpp"
#include "ecs/components/EmoteCatalog.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

/// @brief Client-only input sampling system — split into two halves so mouse
///        look can run every iterate() (smooth camera at any FPS) while
///        movement keys run once per physics tick group (server-consistent).
namespace systems
{

/// @brief Tracks previous-frame key state for edge detection.
inline bool prevKillSelfKey = false;
/// @brief Tracks previous-frame G (CycleGrenade) key state for tap-throw vs hold-cycle.
inline bool prevGrenadeKey = false;
/// @brief True once a cycle (next/prev) fired during the current G hold — suppresses throw on release.
inline bool grenadeCycledThisHold = false;
/// @brief Previous-frame Shoot/Scope state while G held, for cycle edge detection (keyboard/mouse).
inline bool prevGrenadeCycleNext = false;
inline bool prevGrenadeCyclePrev = false;
/// @brief Gamepad equivalents of the above for the hold-(D-pad Left) + RT/LT cycle chord.
inline bool prevGamepadGrenadeKey = false;
inline bool gamepadGrenadeCycledThisHold = false;
inline bool prevGamepadGrenadeCycleNext = false;
inline bool prevGamepadGrenadeCyclePrev = false;
/// @brief Latched throw request, consumed once per frame by the game loop.
inline bool pendingGrenadeThrow = false;
/// @brief Latched cycle next/prev requests, consumed once per frame by the game loop.
inline bool pendingGrenadeCycleNext = false;
inline bool pendingGrenadeCyclePrev = false;
/// @brief Tracks previous-frame Alt+LMB state for ability choice edge detection.
inline bool prevAbilitySelectLeft = false;
/// @brief Tracks previous-frame Alt+RMB state for ability choice edge detection.
inline bool prevAbilitySelectRight = false;
/// @brief Tracks previous-frame gamepad left ability-select chord for edge detection.
inline bool prevGamepadAbilitySelectLeft = false;
/// @brief Tracks previous-frame gamepad right ability-select chord for edge detection.
inline bool prevGamepadAbilitySelectRight = false;

// ─── Emote wheel state ───────────────────────────────────────────────────────
//
// The emote wheel mirrors the radial-selection feel of a grenade wheel: hold the
// Emote binding to open the wheel, point (mouse delta on KBM, right stick on a
// pad) at one of the `emotes::kEmoteCount` sectors to highlight it, and release
// to play the highlighted emote. The highlighted sector and a latched
// "play this emote" request are read by the game loop and the HUD widget.

/// @brief True while the emote wheel is open (Emote binding held on any device).
inline bool emoteWheelOpen = false;
/// @brief Currently highlighted emote sector while the wheel is open, or -1 (none).
inline int emoteWheelSelection = -1;
/// @brief Accumulated pointing direction used to resolve the highlighted sector.
inline float emoteWheelDirX = 0.0f;
inline float emoteWheelDirY = 0.0f;
/// @brief Latched emote to play on wheel release; consumed once by the game loop.
inline int pendingEmoteRequest = -1;
/// @brief Previous-frame Emote-binding state for open/close edge detection.
inline bool prevEmoteKey = false;
inline bool prevGamepadEmoteKey = false;

/// @brief Gamepad axis mapping configuration for look and move axes, used to support stick swapping in user settings.
struct JoystickAxis
{
    SDL_GamepadAxis x;
    SDL_GamepadAxis y;
};

/// @brief Return the SDL axes used for camera look, honoring the stick-swap setting.
inline JoystickAxis getLookJoystickAxes(bool swapSticks)
{
    return {
        .x = swapSticks ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX,
        .y = swapSticks ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY,
    };
}

/// @brief Return the SDL axes used for movement, honoring the stick-swap setting.
inline JoystickAxis getMoveJoystickAxes(bool swapSticks)
{
    return {
        .x = swapSticks ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX,
        .y = swapSticks ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY,
    };
}

/// @brief True when a gamepad handle is non-null and still connected.
inline bool gamepadConnected(SDL_Gamepad* gamepad)
{
    return gamepad != nullptr && SDL_GamepadConnected(gamepad);
}

/// @brief Consume and clear the queued grenade throw request.
inline bool consumePendingGrenadeThrow()
{
    const bool shouldThrow = pendingGrenadeThrow;
    pendingGrenadeThrow = false;
    return shouldThrow;
}

/// @brief Consume and clear the queued "cycle to next grenade" request.
inline bool consumePendingGrenadeCycleNext()
{
    const bool requested = pendingGrenadeCycleNext;
    pendingGrenadeCycleNext = false;
    return requested;
}

/// @brief Consume and clear the queued "cycle to previous grenade" request.
inline bool consumePendingGrenadeCyclePrev()
{
    const bool requested = pendingGrenadeCyclePrev;
    pendingGrenadeCyclePrev = false;
    return requested;
}

/// @brief Consume and clear the latched emote-play request (-1 = none).
inline int consumePendingEmote()
{
    const int emote = pendingEmoteRequest;
    pendingEmoteRequest = -1;
    return emote;
}

/// @brief Resolve `emoteWheelSelection` from a pointing input.
///
/// @param dx,dy      Pointing delta in screen convention (dx right, dy down).
/// @param accumulate True for mouse deltas (integrated into a virtual stick),
///                   false for an absolute stick direction.
///
/// The wheel lays emote 0 at the top and proceeds clockwise. A small deadzone
/// keeps the selection empty until the player points clearly in a direction.
inline void emoteWheelApplyPointing(float dx, float dy, bool accumulate)
{
    if (accumulate) {
        emoteWheelDirX += dx;
        emoteWheelDirY += dy;
    } else {
        emoteWheelDirX = dx;
        emoteWheelDirY = dy;
    }

    // Clamp magnitude so accumulated mouse motion can't grow without bound.
    constexpr float k_maxMag = 400.0f;
    const float mag2 = emoteWheelDirX * emoteWheelDirX + emoteWheelDirY * emoteWheelDirY;
    if (mag2 > k_maxMag * k_maxMag) {
        const float m = std::sqrt(mag2);
        emoteWheelDirX *= k_maxMag / m;
        emoteWheelDirY *= k_maxMag / m;
    }

    constexpr float k_deadzone = 28.0f;
    if (mag2 < k_deadzone * k_deadzone) {
        emoteWheelSelection = -1;
        return;
    }

    // atan2(dx, -dy): 0 at top (pointing up), increasing clockwise.
    float angle = std::atan2(emoteWheelDirX, -emoteWheelDirY);
    if (angle < 0.0f)
        angle += glm::two_pi<float>();
    const float sectorSize = glm::two_pi<float>() / static_cast<float>(emotes::kEmoteCount);
    emoteWheelSelection = static_cast<int>(std::lround(angle / sectorSize)) % emotes::kEmoteCount;
}

/// @brief Track the Emote binding (keyboard/mouse) to open/close the wheel.
///
/// Must run every iterate() before runMouseLook so the look sampler can divert
/// mouse motion into the wheel. On release, the highlighted sector is latched
/// as the emote to play.
inline void runEmoteWheelKey(const InputBindings& bindings)
{
    const bool* const kKeys = SDL_GetKeyboardState(nullptr);
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);
    const bool keyNow = bindings.pressed(Action::Emote, kKeys, mouse);

    if (keyNow && !prevEmoteKey) {
        // Opening: clear any stale pointing/selection.
        emoteWheelDirX = 0.0f;
        emoteWheelDirY = 0.0f;
        emoteWheelSelection = -1;
    } else if (!keyNow && prevEmoteKey) {
        // Releasing: fire the highlighted emote (if any).
        if (emoteWheelSelection >= 0)
            pendingEmoteRequest = emoteWheelSelection;
        emoteWheelSelection = -1;
    }

    emoteWheelOpen = keyNow;
    prevEmoteKey = keyNow;
}

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

    // While the emote wheel is open, mouse motion picks a sector instead of
    // turning the camera (standard radial-menu feel).
    if (emoteWheelOpen) {
        emoteWheelApplyPointing(mdx, mdy, /*accumulate=*/true);
        return;
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

/// @brief Sample keyboard state into the movement flags.
///
/// Should be called **once per physics tick group** when input is synced
/// with physics (the default) so movement calculations match the server.
/// Can also be called every iterate() when the sync toggle is off.
///
/// @param registry        The ECS registry.
/// @param bindings        Keyboard/mouse bindings to sample.
/// @param gravityFlipped  When true, A/D are swapped so strafing feels
///                        correct while the camera is rolled 180°.
inline void runMovementKeys(Registry& registry, const InputBindings& bindings, bool gravityFlipped = false)
{
    const bool* const kKeys = SDL_GetKeyboardState(nullptr);
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);

    // Edge-detect K key: only fire killSelf on the rising edge (key-down),
    // not while held.  Prevents respawn → immediate re-death loop.
    const bool killKeyNow = bindings.pressed(Action::KillSelf, kKeys, mouse);
    const bool killEdge = killKeyNow && !prevKillSelfKey;
    prevKillSelfKey = killKeyNow;

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        // Movement keys
        const bool forward = bindings.pressed(Action::Forward, kKeys, mouse);
        const bool back = bindings.pressed(Action::Back, kKeys, mouse);
        const bool left = bindings.pressed(Action::Left, kKeys, mouse);
        const bool right = bindings.pressed(Action::Right, kKeys, mouse);

        snap.forward = forward;
        snap.back = back;
        // When gravity is flipped the camera is rolled 180°, which negates
        // the screen-space right vector.  Swapping A/D compensates so
        // pressing A still moves the player screen-left.
        snap.left = gravityFlipped ? right : left;
        snap.right = gravityFlipped ? left : right;
        snap.jump = bindings.pressed(Action::Jump, kKeys, mouse);
        snap.crouch = bindings.pressed(Action::Crouch, kKeys, mouse);
        snap.ability1 = bindings.pressed(Action::Ability1, kKeys, mouse);
        snap.ability2 = bindings.pressed(Action::Ability2, kKeys, mouse);
        snap.killSelf = killEdge;
        snap.skipRespawn = false; // Clear stale flag from previous death.
    });
}

/// @brief Sample skip-respawn input while the local player is dead.
///
/// Runs independently of Controllable — dead players can use the Jump binding
/// to skip the remaining respawn timer and respawn immediately.
///
/// @param registry  The ECS registry.
/// @param bindings  The configured keyboard/mouse bindings.
inline void runDeadInput(Registry& registry, const InputBindings& bindings)
{
    const bool* const kKeys = SDL_GetKeyboardState(nullptr);
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);

    registry.view<InputSnapshot, LocalPlayer, RespawnTimer>().each(
        [&](InputSnapshot& snap, const RespawnTimer& /*unused*/) {
            snap.skipRespawn = bindings.pressed(Action::Jump, kKeys, mouse);
        });
}

/// @brief Sample keyboard state into the weapon flags.
///
/// Should be called **once per physics tick group** when input is synced
/// with physics (the default) so movement calculations match the server.
/// Can also be called every iterate() when the sync toggle is off.
///
/// @param registry  The ECS registry.
/// @param bindings  The input bindings.
inline void runWeaponKeys(Registry& registry, const InputBindings& bindings)
{
    const bool* const kKeys = SDL_GetKeyboardState(nullptr);
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);
    const bool abilityMenuHeld = bindings.pressed(Action::AbilityMenu, kKeys, mouse);
    const bool shootDown = bindings.pressed(Action::Shoot, kKeys, mouse);
    const bool scopeDown = bindings.pressed(Action::Scope, kKeys, mouse);
    const bool selectLeftNow = abilityMenuHeld && shootDown;
    const bool selectRightNow = abilityMenuHeld && scopeDown;
    const bool selectLeftEdge = selectLeftNow && !prevAbilitySelectLeft;
    const bool selectRightEdge = selectRightNow && !prevAbilitySelectRight;
    prevAbilitySelectLeft = selectLeftNow;
    prevAbilitySelectRight = selectRightNow;

    // Grenade: hold CycleGrenade (G) as a modifier. While held, Shoot cycles to
    // the next grenade and Scope to the previous (both edge-triggered and
    // suppressed from firing). A quick tap of G with no cycle throws the
    // selected grenade — detected as a release where no cycle fired this hold.
    // Cycle/throw requests are latched here and pulsed once per frame by the
    // game loop (mirrors throwGrenade) so the server applies each exactly once.
    const bool grenadeKeyNow = bindings.pressed(Action::CycleGrenade, kKeys, mouse);
    if (grenadeKeyNow) {
        if (!prevGrenadeKey) {
            grenadeCycledThisHold = false;
        }
        const bool cycleNextNow = shootDown && !abilityMenuHeld;
        const bool cyclePrevNow = scopeDown && !abilityMenuHeld;
        if (cycleNextNow && !prevGrenadeCycleNext) {
            pendingGrenadeCycleNext = true;
            grenadeCycledThisHold = true;
        }
        if (cyclePrevNow && !prevGrenadeCyclePrev) {
            pendingGrenadeCyclePrev = true;
            grenadeCycledThisHold = true;
        }
        prevGrenadeCycleNext = cycleNextNow;
        prevGrenadeCyclePrev = cyclePrevNow;
    } else {
        if (prevGrenadeKey && !grenadeCycledThisHold) {
            pendingGrenadeThrow = true;
        }
        prevGrenadeCycleNext = false;
        prevGrenadeCyclePrev = false;
    }
    prevGrenadeKey = grenadeKeyNow;

    // Suppress fire/aim while the grenade modifier is held — Shoot/Scope are
    // repurposed for cycling, exactly as AbilityMenu repurposes them for ability
    // selection. Also suppress while the emote wheel is open.
    const bool fireSuppressed = abilityMenuHeld || grenadeKeyNow || emoteWheelOpen;

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        snap.shooting = shootDown && !fireSuppressed;
        snap.scoped = scopeDown && !fireSuppressed;
        snap.switchToPrimary = bindings.pressed(Action::SwitchToPrimary, kKeys, mouse);
        snap.switchToSecondary = bindings.pressed(Action::SwitchToSecondary, kKeys, mouse);
        snap.reload = bindings.pressed(Action::Reload, kKeys, mouse);
        snap.pickup = bindings.pressed(Action::Pickup, kKeys, mouse);
        snap.abilitySelectHeld = abilityMenuHeld;
        snap.abilitySelectLeft = selectLeftEdge;
        snap.abilitySelectRight = selectRightEdge;
    });
}

/// @brief Legacy combined sampler — calls both runMouseLook and runMovementKeys.
/// @param registry          The ECS registry.
/// @param mouseSensitivity  Radians per pixel.
inline void runInputSample(
    Registry& registry,
    const InputBindings& bindings,
    float mouseSensitivity = user_settings::kDefaultMouseSensitivity)
{
    runMouseLook(registry, mouseSensitivity);
    runMovementKeys(registry, bindings);
    runWeaponKeys(registry, bindings);
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
inline float normaliseAxis(Sint16 raw, float deadzone = k_stickDeadzone)
{
    // SDL_Gamepad axes are int16; normalise to [-1, 1].  -32768 is one larger
    // in magnitude than 32767 — divide by 32767 and clamp to keep the range
    // symmetric (otherwise full-down on a stick reads as -1.0000305...).
    const float v = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
    if (std::fabs(v) < deadzone)
        return 0.0f;
    // Rescale [deadzone, 1] → [0, 1] so the user gets the full output range.
    const float sign = v < 0.0f ? -1.0f : 1.0f;
    return sign * (std::fabs(v) - deadzone) / (1.0f - deadzone);
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
/// @param pitchSensitivity  Pitch radians per second at full stick deflection.
/// @param yawSensitivity    Yaw radians per second at full stick deflection.
/// @param deadzone          Stick deadzone as a fraction of full deflection.
/// @param dt                Frame delta time in seconds.
/// @param gravityFlipped    When true, both axes are inverted for 180° camera roll.
/// @param swapSticks        When true, use the left stick for look instead of the right stick.
inline void runGamepadLook(Registry& registry,
                           SDL_Gamepad* gamepad,
                           float pitchSensitivity,
                           float yawSensitivity,
                           float deadzone,
                           float dt,
                           bool gravityFlipped = false,
                           bool swapSticks = false)
{
    if (!gamepadConnected(gamepad))
        return;

    // While the emote wheel is open the right stick selects a sector, so don't
    // also turn the camera with it.
    if (emoteWheelOpen)
        return;

    JoystickAxis lookAxis = getLookJoystickAxes(swapSticks);

    float rx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, lookAxis.x), deadzone);
    float ry = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, lookAxis.y), deadzone);

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
        snap.yaw -= rx * yawSensitivity * dt;
        snap.yaw = std::remainder(snap.yaw, glm::radians(360.0f));

        // SDL gamepad Y axis is +down/-up (screen-coord convention) — same as
        // mouse mdy — so adding directly matches mouse-down = pitch+ behaviour.
        snap.pitch = std::clamp(snap.pitch + ry * pitchSensitivity * dt, -glm::radians(89.0f), glm::radians(89.0f));
    });
}

/// @brief Track the Emote binding (controller) to open/close the wheel and pick
///        a sector from the right stick.
///
/// Mirrors runEmoteWheelKey; runs every iterate() before runGamepadLook. ORs
/// into the shared wheel state so a player can use either device. On release the
/// highlighted sector is latched as the emote to play.
inline void runGamepadEmoteWheel(SDL_Gamepad* gamepad, const InputBindings& bindings, bool swapSticks = false)
{
    if (!gamepadConnected(gamepad))
        return;

    const bool padNow = bindings.controllerPressed(Action::Emote, gamepad);
    if (padNow && !prevGamepadEmoteKey) {
        emoteWheelDirX = 0.0f;
        emoteWheelDirY = 0.0f;
        emoteWheelSelection = -1;
    } else if (!padNow && prevGamepadEmoteKey) {
        if (emoteWheelSelection >= 0)
            pendingEmoteRequest = emoteWheelSelection;
        emoteWheelSelection = -1;
    }
    prevGamepadEmoteKey = padNow;

    if (padNow) {
        emoteWheelOpen = true;
        // Right stick gives an absolute direction; scale into the pixel-space
        // units emoteWheelApplyPointing expects so the deadzone lines up.
        const JoystickAxis lookAxis = getLookJoystickAxes(swapSticks);
        const float rx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, lookAxis.x));
        const float ry = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, lookAxis.y));
        emoteWheelApplyPointing(rx * 200.0f, ry * 200.0f, /*accumulate=*/false);
    }
}

/// @brief Sample gamepad buttons / left stick into the movement flags.
///
/// ORs with whatever the keyboard sampler set so kbm + pad coexist.  Should
/// run on the same cadence as runMovementKeys (once per physics tick group
/// when synced, otherwise every iterate).
///
/// Mapping:
///   Left stick     → forward / back / strafe
///   Configured buttons/triggers drive jump, crouch, and abilities.
///
/// @param registry        The ECS registry.
/// @param gamepad          Open gamepad, or nullptr to no-op.
/// @param bindings         Controller bindings to sample.
/// @param deadzone         Stick deadzone as a fraction of full deflection.
/// @param gravityFlipped   When true, left/right stick are swapped to match
///                         the 180° camera roll.
/// @param swapSticks       When true, use the right stick for movement instead of the left stick.
inline void runGamepadMovement(Registry& registry,
                               SDL_Gamepad* gamepad,
                               const InputBindings& bindings,
                               float deadzone,
                               bool gravityFlipped = false,
                               bool swapSticks = false)
{
    if (!gamepadConnected(gamepad))
        return;

    JoystickAxis moveAxis = getMoveJoystickAxes(swapSticks);

    const float lx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, moveAxis.x), deadzone);
    const float ly = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, moveAxis.y), deadzone);

    // Movement booleans are derived from a stronger threshold than the deadzone
    // so a player resting their thumb on the stick doesn't drift-walk.  0.3 is
    // ~tip pressure on a 360 stick.
    constexpr float moveThresh = 0.3f;
    const bool padForward = ly < -moveThresh; // stick-up = -Y in SDL
    const bool padBack = ly > moveThresh;
    // Swap left/right when gravity is flipped (same reasoning as keyboard).
    const bool padLeft = gravityFlipped ? (lx > moveThresh) : (lx < -moveThresh);
    const bool padRight = gravityFlipped ? (lx < -moveThresh) : (lx > moveThresh);

    const bool padJump = bindings.controllerPressed(Action::Jump, gamepad);
    const bool padCrouch = bindings.controllerPressed(Action::Crouch, gamepad);
    const bool padAbility1 = bindings.controllerPressed(Action::Ability1, gamepad);
    const bool padAbility2 = bindings.controllerPressed(Action::Ability2, gamepad);

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        snap.forward |= padForward;
        snap.back |= padBack;
        snap.left |= padLeft;
        snap.right |= padRight;
        snap.jump |= padJump;
        snap.crouch |= padCrouch;
        snap.ability1 |= padAbility1;
        snap.ability2 |= padAbility2;
    });
}

/// @brief Sample gamepad buttons / right trigger into the weapon flags.
///
/// ORs with whatever the keyboard sampler set so kbm + pad coexist.
///
/// Mapping:
///   Configured buttons/triggers drive weapon actions.
///
/// @param registry  The ECS registry.
/// @param gamepad   Open gamepad, or nullptr to no-op.
/// @param bindings  Controller bindings to sample.
inline void runGamepadWeapon(Registry& registry, SDL_Gamepad* gamepad, const InputBindings& bindings)
{
    if (!gamepadConnected(gamepad))
        return;

    const bool abilityMenuHeld = bindings.controllerPressed(Action::AbilityMenu, gamepad);
    const bool padShoot = bindings.controllerPressed(Action::Shoot, gamepad);
    const bool padScope = bindings.controllerPressed(Action::Scope, gamepad);
    const bool padReload = bindings.controllerPressed(Action::Reload, gamepad);
    const bool padPickup = bindings.controllerPressed(Action::Pickup, gamepad);
    const bool padPrimary = bindings.controllerPressed(Action::SwitchToPrimary, gamepad);
    const bool padSecondary = bindings.controllerPressed(Action::SwitchToSecondary, gamepad);
    const bool padCycleGrenade = bindings.controllerPressed(Action::CycleGrenade, gamepad);
    const bool selectLeftNow = abilityMenuHeld && padShoot;
    const bool selectRightNow = abilityMenuHeld && padScope;
    const bool selectLeftEdge = selectLeftNow && !prevGamepadAbilitySelectLeft;
    const bool selectRightEdge = selectRightNow && !prevGamepadAbilitySelectRight;
    prevGamepadAbilitySelectLeft = selectLeftNow;
    prevGamepadAbilitySelectRight = selectRightNow;

    // Grenade: mirror of the keyboard hold-G chord. Hold CycleGrenade (D-pad
    // Left) as a modifier; Right Trigger (Shoot) cycles next, Left Trigger
    // (Scope) cycles previous; a tap with no cycle throws. Latches feed the same
    // once-per-frame pulse as the keyboard path.
    if (padCycleGrenade) {
        if (!prevGamepadGrenadeKey) {
            gamepadGrenadeCycledThisHold = false;
        }
        const bool cycleNextNow = padShoot && !abilityMenuHeld;
        const bool cyclePrevNow = padScope && !abilityMenuHeld;
        if (cycleNextNow && !prevGamepadGrenadeCycleNext) {
            pendingGrenadeCycleNext = true;
            gamepadGrenadeCycledThisHold = true;
        }
        if (cyclePrevNow && !prevGamepadGrenadeCyclePrev) {
            pendingGrenadeCyclePrev = true;
            gamepadGrenadeCycledThisHold = true;
        }
        prevGamepadGrenadeCycleNext = cycleNextNow;
        prevGamepadGrenadeCyclePrev = cyclePrevNow;
    } else {
        if (prevGamepadGrenadeKey && !gamepadGrenadeCycledThisHold) {
            pendingGrenadeThrow = true;
        }
        prevGamepadGrenadeCycleNext = false;
        prevGamepadGrenadeCyclePrev = false;
    }
    prevGamepadGrenadeKey = padCycleGrenade;

    // Suppress fire/aim while either modifier (ability menu or grenade) is held.
    const bool fireSuppressed = abilityMenuHeld || padCycleGrenade;

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        snap.shooting |= padShoot && !fireSuppressed;
        snap.scoped |= padScope && !fireSuppressed;
        snap.reload |= padReload;
        snap.pickup |= padPickup;
        snap.switchToPrimary |= padPrimary;
        snap.switchToSecondary |= padSecondary;
        snap.abilitySelectHeld |= abilityMenuHeld;
        snap.abilitySelectLeft |= selectLeftEdge;
        snap.abilitySelectRight |= selectRightEdge;
    });
}

/// @brief Sample controller skip-respawn input while the local player is dead.
/// @param registry  The ECS registry.
/// @param gamepad   Open gamepad, or nullptr to no-op.
/// @param bindings  Controller bindings to sample.
inline void runGamepadDeadInput(Registry& registry, SDL_Gamepad* gamepad, const InputBindings& bindings)
{
    if (!gamepadConnected(gamepad))
        return;
    const bool skipRespawn = bindings.controllerPressed(Action::Jump, gamepad);
    // OR into the flag the keyboard path (runDeadInput) just set — the gamepad
    // sampler runs right after it, so an assignment here would stomp a Space
    // press on keyboard whenever a pad is connected. Mirrors how the alive-path
    // gamepad inputs compose with |= rather than overwriting.
    registry.view<InputSnapshot, LocalPlayer, RespawnTimer>().each(
        [&](InputSnapshot& snap, const RespawnTimer& /*unused*/) { snap.skipRespawn |= skipRespawn; });
}

} // namespace systems
