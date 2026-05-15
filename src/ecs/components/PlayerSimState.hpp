/// @file PlayerSimState.hpp
/// @brief Server-only locomotion bookkeeping (timers, blacklists, lurch state).
///
/// Counterpart to PlayerVisState. These fields drive the server's
/// MovementSystem / CollisionSystem but are not needed on remote clients —
/// a viewer doesn't care that the player you're watching has 0.13 s of
/// coyote-time remaining or that a particular wall is blacklisted from
/// re-grab. Keeping these out of the per-tick replicated payload is the
/// single biggest bandwidth cut in Phase 2 (272-byte PlayerState → ~64-byte
/// PlayerVisState on the wire, with this ~150-byte struct staying server-
/// side until Phase 5 mirrors it on the owning client for prediction).
///
/// All field semantics are unchanged from the original PlayerState — this
/// is purely a structural split, no behavior change.

#pragma once

#include "PlayerVisState.hpp" // for PlayerStateRef + transitively PlayerStateEnums
#include "ecs/physics/TriMeshCollision.hpp"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

/// @brief Server-only locomotion bookkeeping.
///
/// Read/written by MovementSystem and CollisionSystem on the server every
/// physics tick. Will be mirrored on the owning client in Phase 5 for
/// prediction; remote clients never see this.
struct PlayerSimState
{
    // ── Jump state ─────────────────────────────────────────────────────────
    bool canDoubleJump{true};     ///< Refreshed only after `k_doubleJumpGroundedRefreshTime` of continuous
                                  ///< grounded OnFoot time. Wall jumps, climb jumps, slidehops, ledge mantles,
                                  ///< and instant landings do NOT refresh it.
    bool jumpedThisTick{false};   ///< Set during the tick a jump occurs (for lurch setup).
    bool jumpHeldLastTick{false}; ///< Was jump key held on the previous tick (edge detection).
    float jumpCooldown{0.0f};     ///< Minimum time before double jump is available (s).

    // ── Coyote time ────────────────────────────────────────────────────────
    float coyoteTimer{0.0f};      ///< Remaining grace time after leaving ground/wall (s).
    bool wasGroundedLastTick{false};
    float groundedDuration{0.0f}; ///< Time continuously grounded (s); resets on leaving ground.
                                  ///< Distinguishes fresh ground jumps (lurch-eligible) from
                                  ///< bhop chain continuations.

    // ── Jump lurch ─────────────────────────────────────────────────────────
    bool jumpLurchEnabled{false};     ///< True during the lurch grace window after jumping.
    float jumpLurchTimer{0.0f};       ///< Time elapsed since the jump that enabled lurch (s).
    glm::vec2 moveInputsOnJump{0.0f}; ///< WASD direction when jump started (for direction-change detection).

    // ── Sliding ────────────────────────────────────────────────────────────
    float slideTimer{0.0f};         ///< How long the current slide has lasted (s).
    int slideFatigueCounter{0};     ///< Diminishing returns on consecutive slidehops.
    float slideBoostCooldown{0.0f}; ///< Remaining cooldown before next slide boost (s).
    int slideFatigueDecayAccum{0};  ///< Tick accumulator for fatigue recovery.
    bool canEnterSlide{true};       ///< Cleared when in air, set on landing.

    // ── Wallrunning ────────────────────────────────────────────────────────
    glm::vec3 wallNormal{0.0f};         ///< Normal of the wall being run on.
    glm::vec3 wallForward{0.0f};        ///< Direction of travel along the wall.
    glm::vec3 wallAnchor{0.0f};         ///< Closest point on the attached wall surface.
    float wallRunTimer{0.0f};           ///< Time on current wall (s).
    float wallRunSpeedTimer{0.0f};      ///< Timer for the speed-loss delay.
    float exitWallTimer{0.0f};          ///< Remaining exit-wall grace time (s).
    uint32_t wallMeshIndex{UINT32_MAX}; ///< Current static collision mesh under the wall attachment, if any.
    uint32_t wallTriId{UINT32_MAX};     ///< Current mesh triangle under the wall attachment, if any.
    physics::TriRegion wallRegion{physics::TriRegion::Face}; ///< Closest feature on `wallMeshIndex` / `wallTriId`.
    bool wallAttachmentValid{false};          ///< True while the wallrun has a collision-backed attachment.
    bool wallCornerTransitionActive{false};   ///< True while carrying a pending external-corner handoff.
    glm::vec3 wallCornerAnchor{0.0f};         ///< Corner/seam point used as the transition clearance origin.
    glm::vec3 wallCornerFromNormal{0.0f};     ///< Wall normal held until the capsule clears the old wall edge.
    glm::vec3 wallCornerFromForward{0.0f};    ///< Old wall tangent held during the corner approach.
    glm::vec3 wallCornerToNormal{0.0f};       ///< Pending wall normal to attach after clearance.
    glm::vec3 wallCornerToForward{0.0f};      ///< Pending wall tangent to use after clearance.
    uint32_t wallCornerMeshIndex{UINT32_MAX}; ///< Pending wall mesh after corner clearance.
    uint32_t wallCornerTriId{UINT32_MAX};     ///< Pending wall triangle after corner clearance.
    physics::TriRegion wallCornerRegion{physics::TriRegion::Face}; ///< Pending wall feature after clearance.
    float wallCornerTimer{0.0f};                                   ///< Time spent in the active corner transition (s).
    glm::vec3 wallCornerIgnoreNormal{0.0f}; ///< Source wall briefly ignored after a corner commit.
    float wallCornerIgnoreTimer{0.0f};      ///< Remaining time to suppress source-wall backtracking.
    bool wasWallRunning{false};             ///< Set briefly after leaving wallrun (coyote wall jump).

    // Wall blacklist: stores the last wall's normal + height to prevent regrab.
    glm::vec3 wallBlacklistNormal{0.0f};
    float wallBlacklistHeight{-1e10f};
    bool wallBlacklistActive{false};

    // ── Climbing ───────────────────────────────────────────────────────────
    glm::vec3 climbWallNormal{0.0f};  ///< Normal of the wall being climbed.
    glm::vec3 climbAttachPoint{0.0f}; ///< Surface point where the current climb attached.
    float climbAttachHeight{0.0f};    ///< World Y at climb attach; used for same-wall regrab gating.
    float climbNonUpTimer{0.0f};      ///< Time spent attached without upward climb intent/motion (s).
    float climbTimer{0.0f};           ///< Time on current climb (s).
    bool climbHadUpwardMotion{false}; ///< True once this climb produces local-up velocity.
    float exitClimbTimer{0.0f};
    bool wasClimbing{false};

    // Climb blacklist.
    glm::vec3 climbBlacklistNormal{0.0f};
    float climbBlacklistHeight{-1e10f};
    bool climbBlacklistActive{false};

    // ── Ledge grabbing ─────────────────────────────────────────────────────
    glm::vec3 ledgePoint{0.0f};  ///< World-space position of the grabbed ledge.
    glm::vec3 ledgeNormal{0.0f}; ///< Wall normal at the ledge.
    float ledgeHoldTimer{0.0f};  ///< Time spent holding the ledge (s).
    bool exitingLedge{false};
    float exitLedgeTimer{0.0f};

    // ── Grappling hook (Widowmaker-style: direct pull, look-biased launch) ─
    bool grappleCooldownActive{false}; ///< True during cooldown between uses.
    float grappleCooldownTimer{0.0f};  ///< Remaining cooldown time (s).
    float grapplePullTimer{0.0f};      ///< Time spent being pulled (s).
    glm::vec3 grapplePullDir{0.0f};    ///< Cached pull direction (toward anchor at fire time).
    bool grappleInputLastTick{false};  ///< Edge detection on the grapple key.

    // ── Grapple perch mode (hold jump while grappling → arc above hook) ─
    // No state needed: the trajectory is computed each tick purely from
    // current Position + replicated grapplePoint, so it stays in sync
    // across server simulation, client prediction, and reconciliation
    // replay (PlayerSimState is server-only, so anything stateful here
    // would drift on every snapshot apply and cause visible jitter).
};

/// @brief Combined-reference helper for code that needs both halves.
///
/// Most call sites in MovementSystem.cpp's helper functions used to take a
/// single `PlayerState&` parameter and freely touch any field. After the
/// Phase-2 split they need access to both halves; rather than rewriting
/// every helper signature to take two refs, they take one PlayerStateRef
/// and use `state.vis.X` / `state.sim.X` for the field access. Keeps the
/// migration diff small inside MovementSystem.cpp.
struct PlayerStateRef
{
    PlayerVisState& vis;
    PlayerSimState& sim;
};

/// @brief Read-only counterpart of PlayerStateRef, for `const` consumers.
struct ConstPlayerStateRef
{
    const PlayerVisState& vis;
    const PlayerSimState& sim;
};
