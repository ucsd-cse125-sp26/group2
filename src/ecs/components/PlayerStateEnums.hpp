/// @file PlayerStateEnums.hpp
/// @brief Locomotion mode + wall-side enums shared by PlayerVisState and PlayerSimState.
///
/// Split out of the original PlayerState.hpp as part of Phase 2 of the
/// networking overhaul. Both the replicated half (PlayerVisState — includes
/// moveMode + wallRunSide) and the server-only half (PlayerSimState — refers
/// to WallSide via blacklist logic) need these enum types, so they live in
/// a small leaf header to avoid a circular include between the two state
/// structs.

#pragma once

#include <cstdint>

/// @brief Movement mode — mutually exclusive locomotion states.
enum class MoveMode : uint8_t
{
    OnFoot,        ///< Normal ground/air movement (walk, sprint, crouch, airborne).
    Sliding,       ///< Momentum slide on the ground.
    WallRunning,   ///< Running along a wall surface.
    Climbing,      ///< Climbing vertically up a wall.
    LedgeGrabbing, ///< Holding onto a ledge at the top of a wall.
};

/// @brief Which side a wall is on relative to the player.
enum class WallSide : uint8_t
{
    None,
    Left,
    Right,
};
