/// @file PhaseDiagnostic.hpp
/// @brief Per-tick player physics telemetry for diagnosing phase-through bugs.
///
/// When enabled, every player's per-tick collision state is appended to a
/// CSV file in the working directory: position before / after depen / after
/// bump loop, velocity, movement mode, wallrun side, jump count, depen
/// statistics, and a free-form annotation slot for "interesting" events
/// (wallrun-enter, double-jump, depen-cancelled, suspected-phase, …).
///
/// The detector flags rows where the actual per-tick position delta
/// significantly exceeds the expected delta (`velocity * dt`) — that's the
/// "player teleported through geometry" signature.  Other flagged
/// conditions: depen finding overlap but unable to push (vector cancel),
/// depen needing > 20 units of push (deeper than a normal grazing
/// penetration), bump loop consuming all 4 iterations (player was wedged).
///
/// Output goes to `phase-diag.csv` in the binary's working directory.
/// Open in any spreadsheet; sort / filter by the `flags` column to find
/// the bug moment.  Append-mode: a single play session produces one
/// continuous log; deletion is the user's responsibility.

#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <string_view>

namespace physics::diag
{

/// @brief Bitfield flags attached to each logged row — auto-detected.
enum class PhaseFlag : uint32_t
{
    None = 0,
    Grounded = 1u << 0,
    WallRunning = 1u << 1,
    Sliding = 1u << 2,
    Climbing = 1u << 3,
    LedgeGrabbing = 1u << 4,
    GrappleActive = 1u << 5,
    DoubleJumped = 1u << 6,
    GravityFlipped = 1u << 7,
    /// @brief Depen found overlaps but the aggregated push direction
    /// cancelled out (rare; either trapped between mirrored surfaces or
    /// arithmetic underflow).  The player is left inside geometry.
    DepenCancelled = 1u << 8,
    /// @brief Depen had to push the player by >20 u in one tick — the
    /// player was *deep* inside geometry before depen.
    DeepPenetration = 1u << 9,
    /// @brief The bump loop consumed all 4 iterations and still had
    /// remainingTime > 0 — the player was grinding against complex
    /// geometry that couldn't be resolved in 4 sweeps.
    BumpExhausted = 1u << 10,
    /// @brief Actual per-tick position delta exceeded `velocity * dt`
    /// by > 2× + 5 u — the player likely tunnelled through geometry.
    SuspectedPhase = 1u << 11,
};

inline PhaseFlag operator|(PhaseFlag a, PhaseFlag b) noexcept
{
    return static_cast<PhaseFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline PhaseFlag& operator|=(PhaseFlag& a, PhaseFlag b) noexcept
{
    a = a | b;
    return a;
}
inline bool any(PhaseFlag a) noexcept
{
    return static_cast<uint32_t>(a) != 0u;
}

/// @brief One row of telemetry — captured per player per tick.  All vectors
/// are in world coordinates (Y-up, Quake units).
struct PlayerFrame
{
    uint64_t tick = 0;
    entt::entity entity{entt::null};
    glm::vec3 posBefore{0.0f};       ///< Position at tick start, BEFORE depen.
    glm::vec3 posAfterDepen{0.0f};   ///< After depen, BEFORE bump loop.
    glm::vec3 posAfter{0.0f};        ///< Final position, after bump loop + slope snap.
    glm::vec3 velBefore{0.0f};       ///< Velocity at tick start (already integrated by movement).
    glm::vec3 velAfter{0.0f};        ///< Velocity at tick end.
    glm::vec3 lastHitNormal{0.0f};   ///< Normal of the last sweep hit in the bump loop (or 0).
    float depenPushDistance = 0.0f;  ///< |posAfterDepen - posBefore|.
    int bumpHits = 0;                ///< Number of bump iterations that hit something.
    int moveMode = 0;                ///< MoveMode enum cast to int.
    int wallrunSide = 0;             ///< WallSide enum (None=0, Left=1, Right=2).
    int jumpCount = 0;
    PhaseFlag flags = PhaseFlag::None;
    char note[48] = {0};             ///< Free-form annotation slot (e.g., "wallrun-enter").
};

/// @brief Enable / disable telemetry.  When disabled, every `recordFrame`
/// call is a wait-free no-op.  Toggle from DebugUI.
void setEnabled(bool on) noexcept;
[[nodiscard]] bool isEnabled() noexcept;

/// @brief Append a player's per-tick frame to the open CSV log.  Opens the
/// log lazily on first call.  Thread-safe (single global mutex on the
/// write path — diagnostic only, no perf concern).
void recordFrame(const PlayerFrame& frame) noexcept;

/// @brief Attach a text annotation to the NEXT frame recorded for the
/// given entity.  Used by MovementSystem hooks (wallrun enter / exit,
/// double-jump fired, etc.) to correlate movement events with collision
/// state in the log.
void annotate(entt::entity entity, std::string_view label) noexcept;

/// @brief Drain any queued annotation for `entity` into `out`, then clear
/// it.  Internal — called by `recordFrame` to copy the pending note into
/// the row about to be written.  Exposed for unit tests.
void consumeAnnotation(entt::entity entity, char (&out)[48]) noexcept;

} // namespace physics::diag
