/// @file PlayerVisState.hpp
/// @brief Visible / replicated half of the player locomotion state.
///
/// The original PlayerState was ~272 bytes of mixed gameplay-visible state
/// (grounded, sprinting, current move mode, camera tilt) and server-only
/// bookkeeping (timer fields, blacklists, lurch grace windows, grapple
/// cooldowns, slide fatigue counters). Phase 2 of the networking overhaul
/// splits it cleanly so the server only ships the small "what does the
/// renderer / animator / HUD need to see" half to clients.
///
/// Fields here are everything a remote viewer needs to:
///   * pose the character animator (grounded / sprinting / crouching /
///     moveMode / wallRunSide drive locomotion + special-mode states),
///   * draw the grapple cable (grappleActive + grapplePoint),
///   * compute camera roll for first-person wallrun lean (targetCameraTilt),
///   * orient feet against the floor surface (groundNormal),
///   * show the death state in the HUD / scoreboard (isDead).
///
/// Anything **only** the simulation needs to compute the next tick lives in
/// PlayerSimState (server-only; mirrored on the owning client when Phase 5
/// prediction lands).
///
/// @note Field-level bit-packing and quantization are deferred to Phase 4
/// (delta encoding). Today the struct is a flat ~64 bytes; even un-packed
/// it is already a >4× reduction on what we used to ship per player.

#pragma once

#include "PlayerStateEnums.hpp"

#include <cstdint>
#include <glm/vec3.hpp>

/// @brief Replicated subset of player locomotion state.
///
/// Read by the renderer, animator, HUD, and debug UI on every connected
/// client. Written by the server's MovementSystem / CollisionSystem each
/// physics tick.
struct PlayerVisState
{
    // Mode flags
    MoveMode moveMode{MoveMode::OnFoot};  ///< Active locomotion mode.
    WallSide wallRunSide{WallSide::None}; ///< Wall side during WallRunning mode.
    int jumpCount{0};                     ///< 0 = on ground, 1 = first jump, 2 = double jumped.

    // Boolean flags (un-packed in Phase 2; bit-packed in Phase 4).
    bool isDead{false};          ///< True while the player is dead and waiting to respawn.
    bool grounded{false};        ///< True when touching a floor surface this tick.
    bool crouching{false};       ///< True when crouch input is held.
    bool sprinting{false};       ///< True when sprint is active.
    bool ads{false};             ///< True when ADS-ing a precision (charge) weapon — caps wish speed
                                 ///< to k_adsSpeed. Derived each tick from input.scoped + equipped weapon.
    bool pendingUncrouch{false}; ///< Deferred uncrouch (e.g. after slidehop); applied when safe.
    bool exitingWall{false};     ///< Brief grace window after leaving a wall.
    bool grappleActive{false};   ///< True while being pulled toward the grapple anchor.
    bool gravityFlipped{false};  ///< True when the player's gravity is inverted (walking on ceilings).

    // Spatial state needed by client renderer/animator.
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f}; ///< Normal of the floor surface for foot orientation.
    glm::vec3 grapplePoint{0.0f};             ///< World-space anchor; renderer draws the cable to here.

    // Cosmetic emote (full-body dance/taunt). Server-authoritative: set from
    // InputSnapshot::emoteRequest, cleared on movement/combat input or death.
    // Drives the server animator's override clip so the resulting AnimSnapshot
    // makes every remote client see the emote. -1 = not emoting.
    std::int8_t activeEmote{-1}; ///< Active emote index (EmoteCatalog), or -1.

    // Camera effects (read by renderer).
    float targetCameraTilt{0.0f}; ///< Target camera roll for wallrun lean (degrees).

    // Spawn facing. Set by the server at (re)spawn to the spawn point's authored
    // yaw; the local client snaps its view to this on the dead→alive edge so the
    // player doesn't respawn looking into a wall.
    float spawnViewYaw{0.0f}; ///< Authored spawn facing (radians); applied to local view on respawn.
};
