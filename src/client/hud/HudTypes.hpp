/// @file HudTypes.hpp
/// @brief Shared types for the HUD system.

#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <string>

// ── Colors ──────────────────────────────────────────────────────────────────

/// @brief RGBA color for HUD elements (linear space, straight alpha).
struct HudColor
{
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;

    constexpr HudColor() = default;
    constexpr HudColor(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr HudColor white() { return {1, 1, 1, 1}; }
    static constexpr HudColor black() { return {0, 0, 0, 1}; }
    static constexpr HudColor red() { return {1, 0, 0, 1}; }
    static constexpr HudColor green() { return {0, 1, 0, 1}; }
    static constexpr HudColor yellow() { return {1, 1, 0, 1}; }
    static constexpr HudColor cyan() { return {0, 1, 1, 1}; }
    static constexpr HudColor transparent() { return {0, 0, 0, 0}; }
};

// ── Enums ───────────────────────────────────────────────────────────────────

enum class HudAnchor
{
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

enum class HudAlign
{
    Left,
    Center,
    Right,
};

/// @brief Icon identifiers for the HUD icon atlas.
enum class HudIcon : uint8_t
{
    // Placeholder — populated when the icon atlas is authored.
    None = 0,
};

// ── Vertex ──────────────────────────────────────────────────────────────────

/// @brief Per-vertex data for all HUD geometry (48 bytes).
struct HudVertex
{
    float position[2];  ///< Pixel coordinates, origin top-left.
    float uv[2];        ///< Texture coords (atlas UV or local quad pos for shapes).
    float color[4];     ///< RGBA, straight alpha.
    float texMode;      ///< 0=solid, 1=SDF text, 2=sprite, 3=SDF rounded rect.
    float shapeData[3]; ///< Mode 3 only: [halfWidth, halfHeight, cornerRadius] in pixels.
};

// ── Crosshair ───────────────────────────────────────────────────────────────

/// @brief Crosshair appearance parameters.
struct CrosshairStyle
{
    float gap = 3.f;                      ///< Gap from center to start of each line (pixels).
    float length = 6.f;                   ///< Length of each crosshair arm (pixels).
    float thickness = 2.f;                ///< Line thickness (pixels).
    HudColor color{0.f, 1.f, 0.f, 0.85f}; ///< Default green.
    bool dot = true;                      ///< Draw center dot.
};

// ── Game State Contract ─────────────────────────────────────────────────────

/// @brief Kill feed entry data.
struct HudKillFeedEntry
{
    std::string killerName;
    std::string victimName;
    int weaponId = 0;
    bool isHeadshot = false;
};

/// @brief Damage direction indicator.
struct HudDamageEvent
{
    float angleDeg = 0.f; ///< Direction the damage came from (degrees, 0=front, CW).
    float amount = 0.f;   ///< Damage amount (for intensity scaling).
};

/// @brief Hit confirmation event.
struct HudHitConfirm
{
    bool isHeadshot = false;
    bool isKill = false;
    bool shieldBreak = false; ///< True when this shot depleted the target's armor.
};

/// @brief Floating damage number spawned at a world-space hit position.
///
/// Projected to screen space each frame by the damage number widget.
struct HudDamageNumber
{
    float worldX = 0.f, worldY = 0.f, worldZ = 0.f; ///< World-space hit position.
    int damage = 0;                                 ///< Damage dealt.
    bool headshot = false;
    bool shielded = false;                          ///< True if target had armor when hit.
};

/// @brief Current damage accumulator state for the local player.
struct HudDamageAccum
{
    int total = 0;                      ///< Accumulated damage to current target.
    HudColor color{1.f, 1.f, 1.f, 1.f}; ///< Color matching the latest hit type.
};

/// @brief Per-teammate status (for scoreboard / team bar).
struct HudTeamMemberStatus
{
    std::string name;
    int health = 100;
    bool isAlive = true;
    int kills = 0;
    int deaths = 0;
    int ping = 0;
};

/// @brief World-space position for minimap display.
struct HudMinimapDot
{
    float worldX = 0.f, worldZ = 0.f;
};

/// @brief Snapshot of game state consumed by the HUD each frame.
///
/// Filled by Game from ECS data. The HUD never imports ECS headers.
struct HudGameState
{
    int health = 100, maxHealth = 100;
    int armor = 0, maxArmor = 100;
    int ammoClip = 30, ammoReserve = 90;
    int weaponId = 0;
    float roundTimeRemaining = 0.f;
    bool isAlive = true;
    bool isBuyPhase = false;

    // Events (valid for this frame only).
    std::span<const HudKillFeedEntry> killFeedEvents;
    std::span<const HudDamageEvent> damageEvents;
    std::span<const HudHitConfirm> hitConfirms;
    std::span<const HudDamageNumber> damageNumbers; ///< Floating damage numbers to spawn this frame.
    HudDamageAccum damageAccum;                     ///< Running damage total to current target.

    // View/projection for world→screen projection (damage numbers).
    glm::mat4 viewProj{1.f};

    // Team status.
    std::span<const HudTeamMemberStatus> allies;
    std::span<const HudTeamMemberStatus> enemies;
    int allyScore = 0, enemyScore = 0;

    // Minimap.
    float localPlayerX = 0.f, localPlayerZ = 0.f;
    std::span<const HudMinimapDot> enemyDots;
    float minimapWorldRange = 1000.f; ///< World units visible in each direction from center.

    // Vignette events (set by Game each frame based on health/armor deltas).
    bool tookDamage = false;     ///< True the frame health or armor decreased.
    float damageIntensity = 0.f; ///< 0..1 fraction of max-health lost this frame.
    bool armorBroke = false;     ///< True the frame armor dropped to zero.
    // isAlive already covers death vignette.

    // Screen dimensions (set by Game each frame).
    float screenW = 1280.f, screenH = 720.f;
};
