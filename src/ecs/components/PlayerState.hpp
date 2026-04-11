#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

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

/// @brief Locomotion state for a player entity.
///
/// Read/written by MovementSystem and CollisionSystem.
/// Extended for Titanfall-style movement mechanics.
struct PlayerState
{
    // ── Core state ──────────────────────────────────────────────────────
    MoveMode moveMode{MoveMode::OnFoot};
    bool grounded{false};            ///< True when touching a floor surface this tick.
    bool crouching{false};           ///< True when crouch input is held.
    bool sprinting{false};           ///< True when sprint is active.
    bool pendingUncrouch{false};     ///< Deferred uncrouch (e.g. after slidehop); applied when safe.
    glm::vec3 groundNormal{0, 1, 0}; ///< Normal of the floor surface we're standing on.

    // ── Jump state ──────────────────────────────────────────────────────
    bool canDoubleJump{true};     ///< Reset on land / wallrun / climb.
    bool jumpedThisTick{false};   ///< Set during the tick a jump occurs (for lurch setup).
    int jumpCount{0};             ///< 0 = on ground, 1 = first jump, 2 = double jumped.
    bool jumpHeldLastTick{false}; ///< Was jump key held on the previous tick (for edge detection).
    float jumpCooldown{0.0f};     ///< Minimum time before double jump is available (s).

    // ── Coyote time ─────────────────────────────────────────────────────
    float coyoteTimer{0.0f}; ///< Remaining grace time after leaving ground/wall (s).
    bool wasGroundedLastTick{false};

    // ── Jump lurch ──────────────────────────────────────────────────────
    bool jumpLurchEnabled{false};     ///< True during the lurch grace window after jumping.
    float jumpLurchTimer{0.0f};       ///< Time elapsed since the jump that enabled lurch (s).
    glm::vec2 moveInputsOnJump{0.0f}; ///< WASD direction when jump started (for detecting direction change).

    // ── Sliding ─────────────────────────────────────────────────────────
    float slideTimer{0.0f};         ///< How long the current slide has lasted (s).
    int slideFatigueCounter{0};     ///< Diminishing returns on consecutive slidehops.
    float slideBoostCooldown{0.0f}; ///< Remaining cooldown before next slide boost (s).
    int slideFatigueDecayAccum{0};  ///< Tick accumulator for fatigue recovery.
    bool canEnterSlide{true};       ///< Cleared when in air, set on landing.

    // ── Wallrunning ─────────────────────────────────────────────────────
    WallSide wallRunSide{WallSide::None};
    glm::vec3 wallNormal{0.0f};    ///< Normal of the wall being run on.
    glm::vec3 wallForward{0.0f};   ///< Direction of travel along the wall.
    float wallRunTimer{0.0f};      ///< Time on current wall (s).
    float wallRunSpeedTimer{0.0f}; ///< Timer for the speed-loss delay.
    bool exitingWall{false};       ///< True for a brief period after leaving a wall.
    float exitWallTimer{0.0f};     ///< Remaining exit-wall time (s).
    bool wasWallRunning{false};    ///< Set briefly after leaving wallrun (for coyote wall jump).

    // Wall blacklist: stores the last wall's normal + height to prevent regrab.
    glm::vec3 wallBlacklistNormal{0.0f};
    float wallBlacklistHeight{-1e10f};
    bool wallBlacklistActive{false};

    // ── Climbing ────────────────────────────────────────────────────────
    glm::vec3 climbWallNormal{0.0f}; ///< Normal of the wall being climbed.
    float climbTimer{0.0f};          ///< Time on current climb (s).
    bool exitingClimb{false};
    float exitClimbTimer{0.0f};
    bool wasClimbing{false};

    // Climb blacklist.
    glm::vec3 climbBlacklistNormal{0.0f};
    float climbBlacklistHeight{-1e10f};
    bool climbBlacklistActive{false};

    // ── Ledge grabbing ──────────────────────────────────────────────────
    glm::vec3 ledgePoint{0.0f};  ///< World-space position of the grabbed ledge.
    glm::vec3 ledgeNormal{0.0f}; ///< Wall normal at the ledge.
    float ledgeHoldTimer{0.0f};  ///< Time spent holding the ledge (s).
    bool exitingLedge{false};
    float exitLedgeTimer{0.0f};

    // ── Camera effects (read by renderer, written by movement) ──────────
    float targetCameraTilt{0.0f}; ///< Target camera roll for wallrun lean (degrees).
};
