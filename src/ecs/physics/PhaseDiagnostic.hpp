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
    /// @brief A position, velocity, normal, or stored movement vector was
    /// non-finite. This is the "stop and inspect nearby rows" failure mode.
    InvalidState = 1u << 12,
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
    glm::vec3 posBefore{0.0f};      ///< Position at tick start, BEFORE depen.
    glm::vec3 posAfterDepen{0.0f};  ///< After depen, BEFORE bump loop.
    glm::vec3 posAfter{0.0f};       ///< Final position, after bump loop + slope snap.
    glm::vec3 velBefore{0.0f};      ///< Velocity at tick start (already integrated by movement).
    glm::vec3 velAfter{0.0f};       ///< Velocity at tick end.
    glm::vec3 lastHitNormal{0.0f};  ///< Normal of the last sweep hit in the bump loop (or 0).
    float depenPushDistance = 0.0f; ///< |posAfterDepen - posBefore|.
    int bumpHits = 0;               ///< Number of bump iterations that hit something.
    int moveMode = 0;               ///< MoveMode enum cast to int.
    int wallrunSide = 0;            ///< WallSide enum (None=0, Left=1, Right=2).
    int jumpCount = 0;
    PhaseFlag flags = PhaseFlag::None;
    char note[48] = {0}; ///< Free-form annotation slot (e.g., "wallrun-enter").
};

/// @brief Enable / disable telemetry.  When disabled, every `recordFrame`
/// call is a wait-free no-op.  Toggle from DebugUI.
void setEnabled(bool on) noexcept;
[[nodiscard]] bool isEnabled() noexcept;

/// @brief Append a player's per-tick frame to the open CSV log.  Opens the
/// log lazily on first call.  Thread-safe (single global mutex on the
/// write path — diagnostic only, no perf concern).
void recordFrame(const PlayerFrame& frame) noexcept;

/// @brief One row captured around MovementSystem, before CollisionSystem/KCC.
///
/// This complements `PlayerFrame`: movement is where climb, ledge, grapple,
/// jump, and wallrun state mutate velocity. If a stored wall/ledge normal
/// becomes NaN, this row catches it before the collision step propagates it
/// into position.
struct MovementFrame
{
    entt::entity entity{entt::null};
    glm::vec3 posBefore{0.0f};
    glm::vec3 posAfter{0.0f};
    glm::vec3 velBefore{0.0f};
    glm::vec3 velAfter{0.0f};
    int modeBefore = 0;
    int modeAfter = 0;
    bool groundedBefore = false;
    bool groundedAfter = false;
    bool inputForward = false;
    bool inputBack = false;
    bool inputLeft = false;
    bool inputRight = false;
    bool inputJump = false;
    bool inputCrouch = false;
    bool inputGrapple = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool wallFront = false;
    bool ledgeDetected = false;
    float groundDistance = 1e30f;
    glm::vec3 frontNormal{0.0f};
    glm::vec3 frontPoint{0.0f};
    glm::vec3 ledgeNormal{0.0f};
    glm::vec3 ledgePoint{0.0f};
    glm::vec3 climbWallNormal{0.0f};
    glm::vec3 storedLedgeNormal{0.0f};
    glm::vec3 storedLedgePoint{0.0f};
    float climbTimer = 0.0f;
    float ledgeHoldTimer = 0.0f;
    PhaseFlag flags = PhaseFlag::None;
    char note[64] = {0};
};

/// @brief Append a MovementSystem telemetry row to
/// `movement-diag-<timestamp>.csv`. No-op unless telemetry is enabled.
void recordMovementFrame(const MovementFrame& frame) noexcept;

/// @brief Attach a text annotation to the NEXT frame recorded for the
/// given entity.  Used by MovementSystem hooks (wallrun enter / exit,
/// double-jump fired, etc.) to correlate movement events with collision
/// state in the log.
void annotate(entt::entity entity, std::string_view label) noexcept;

/// @brief Drain any queued annotation for `entity` into `out`, then clear
/// it.  Internal — called by `recordFrame` to copy the pending note into
/// the row about to be written.  Exposed for unit tests.
void consumeAnnotation(entt::entity entity, char (&out)[48]) noexcept;

/// @brief One row of depen-contact telemetry.  Emitted by the trimesh
/// depen kernel when a per-triangle MTV depth significantly exceeds the
/// player's Minkowski half-radius for that triangle's normal — i.e., the
/// player center is on the BACK side of the face plane (`signedDist < 0`),
/// which is geometrically possible only against back-facing duplicate
/// triangles, inverted-winding tris, or a real (multi-tick) tunnel.
struct DepenContact
{
    uint32_t triId = 0;
    glm::vec3 playerPos{0.0f};  ///< Player capsule center at the moment of overlap.
    glm::vec3 faceNormal{0.0f}; ///< Cooked face normal of the offending triangle.
    glm::vec3 v0{0.0f};
    glm::vec3 v1{0.0f};
    glm::vec3 v2{0.0f};
    float signedDist = 0.0f; ///< `dot(faceN, playerPos - v0)`. Negative ⇒ player on back side.
    float minkowskiR = 0.0f; ///< `|faceN|·halfExtents` — depth would saturate at `2·R` (s = -R).
    float depth = 0.0f;      ///< MTV magnitude = `R - signedDist`.
    int region = 0; ///< Closest feature on the triangle: 0=Face, 1=Edge0, 2=Edge1, 3=Edge2, 4=Vert0, 5=Vert1, 6=Vert2.
    uint8_t edgeFlags = 0; ///< Cooked active-edge mask (bit i ⇔ edge i active per Phase 2 welding).
    uint8_t vertFlags = 0; ///< Cooked active-vertex mask.
};

/// @brief Append one depen-contact row to its own CSV log
/// (`depen-trace-<timestamp>.csv` in the working dir).  Called from the
/// trimesh depen kernel only for "suspicious" contacts (depth ≫ R) so the
/// log stays small.  Lazy file open; thread-safe via a separate mutex from
/// the per-tick frame log.  No-op when telemetry is disabled.
void recordDepenContact(const DepenContact& contact) noexcept;

} // namespace physics::diag
