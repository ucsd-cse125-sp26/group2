/// @file ShotDebugReport.hpp
/// @brief Wire-format types for the PR-20 lag-comp shot debug visualizer.
///
/// Carries everything a single hitscan shot resolved on the server
/// needs to be drawn on the shooter's screen alongside the client's
/// own snapshot of the same moment:
///
///   - shooter origin + direction + range  (server's view)
///   - hit target (or sentinel for miss) + hit point + body region
///   - per in-range target's REWOUND capsule list (the historical
///     sample the server actually raycast against)
///
/// Combined with the client's own captured "where I aimed" + "what
/// I saw the targets looking like at fire-time", the debug UI
/// renders blue (client) vs red (server) capsules and rays, so the
/// player can SEE lag-comp alignment errors directly.
///
/// Sent ONLY to the shooting client.  Not broadcast — every other
/// client has zero use for this data and the bandwidth would scale
/// quadratically with player count.
///
/// Wire layout (POD, little-endian — all hosts in this project are
/// LE x86 / arm64; this isn't a portable wire spec):
///
///   struct ShotDebugReport {
///       uint32_t  shotInputTick;       // matches client's clientPredictTick at fire
///       uint16_t  hitTargetClientId;   // 0xFFFF = miss
///       uint8_t   hitRegion;           // BodyRegion enum value, 0 on miss
///       uint8_t   numTargets;          // count of TargetSnapshot blocks following
///       float     originX, originY, originZ;   // server's eye position at fire
///       float     dirX, dirY, dirZ;            // server's shot direction (unit)
///       float     range;                       // hitscan range
///       float     hitX, hitY, hitZ;            // server's resolved hit point
///       // numTargets × {
///       //     uint16_t targetClientId;
///       //     uint8_t  numCapsules;        // ~12 typical
///       //     uint8_t  _pad;
///       //     numCapsules × WireCapsule
///       // }
///   }
///
/// One report at N=12 capsules × 1 target = 16-byte header + per-target
/// header (4 B) + capsule (32 B × 12 = 384 B) ≈ 410 bytes / shot.
/// At 10 shots/sec from one player = 4 KB/s on the shooter's downlink.
/// Negligible.

#pragma once

#include "ecs/components/Hitbox.hpp" // WorldCapsule

#include <cstdint>
#include <glm/vec3.hpp>
#include <vector>

namespace net::shotdebug
{

/// @brief Sentinel `hitTargetClientId` meaning "shot missed all targets".
inline constexpr std::uint16_t k_missClientId = 0xFFFFu;

/// @brief One capsule on the wire.  Matches the runtime `WorldCapsule`
/// in `ecs/components/Hitbox.hpp` minus the unused tail-padding.
struct WireCapsule
{
    float pointAx;
    float pointAy;
    float pointAz;
    float pointBx;
    float pointBy;
    float pointBz;
    float radius;
    std::uint8_t region; ///< `BodyRegion` enum value.
    std::uint8_t _pad0;  ///< Manual alignment.
    std::uint16_t _pad1;
};
static_assert(sizeof(WireCapsule) == 32, "WireCapsule must be 32 B for the wire format");

/// @brief Header that precedes the variable-length per-target body of
/// the SHOT_DEBUG_REPORT packet.  Fields are in dependency order so
/// the layout is readable in a hex dump.
struct ReportHeader
{
    std::uint32_t shotInputTick;
    std::uint16_t hitTargetClientId;
    std::uint8_t hitRegion;
    std::uint8_t numTargets;
    float originX, originY, originZ;
    float dirX, dirY, dirZ;
    float range;
    float hitX, hitY, hitZ;
};
static_assert(sizeof(ReportHeader) == 48, "ReportHeader layout must be 48 B");

/// @brief Per-target sub-header preceding the variable-length capsule list.
struct TargetHeader
{
    std::uint16_t targetClientId;
    std::uint8_t numCapsules;
    std::uint8_t _pad;
};
static_assert(sizeof(TargetHeader) == 4, "TargetHeader layout must be 4 B");

/// @brief Server-side runtime capture of one shot's debug data,
/// produced by `WeaponSystem::handleFire` and consumed by ServerGame
/// (which serialises into the wire form above and sends to the
/// shooter via `Server::enqueueTo`).  Stays in shared code because
/// the same WeaponSystem.cpp is built into both server and client
/// TUs (client uses it for prediction); the client never populates
/// the capture vector — `runWeapon`'s output param defaults to null.
struct ShotDebugCapture
{
    /// @brief The shooter's network ClientId.  Used by ServerGame
    /// to address the unicast SHOT_DEBUG_REPORT packet.
    std::uint16_t shooterClientId = 0;
    std::uint32_t shotInputTick = 0;
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f};
    float range = 0.0f;
    std::uint16_t hitTargetClientId = k_missClientId;
    std::uint8_t hitRegion = 0;
    glm::vec3 hitPoint{0.0f};

    /// @brief Per in-range target's REWOUND capsule list at fire-time.
    /// Populated while the `RewindHitboxesGuard` is active so the
    /// data reflects what the server actually raycast against.
    struct Target
    {
        std::uint16_t clientId;
        std::vector<WorldCapsule> capsules;
    };
    std::vector<Target> targets;
};

} // namespace net::shotdebug
