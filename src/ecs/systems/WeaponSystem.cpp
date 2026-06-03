/// @file WeaponSystem.cpp
/// @brief Weapon state manager system.

#include "ecs/systems/WeaponSystem.hpp"

#include "PlayerStatusSystem.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/BeamLockState.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/GrenadeConfig.hpp"
#include "ecs/components/GrenadeState.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/HitboxHistory.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LagCompTarget.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/LagCompensation.hpp"

// PR-18b: server-side shot-resolution log.  Server-only — the
// client TU compiles this same file but doesn't have `perf/ShotLog`
// on its include path, so we conditionally include and stub the
// call when missing.  Same `__has_include` pattern as
// RegistrySerialization.cpp's parallel-STL hook.
#if __has_include("perf/ShotLog.hpp")
#include "perf/ShotLog.hpp"
#define GROUP2_HAS_SHOTLOG 1
#else
#define GROUP2_HAS_SHOTLOG 0
#endif

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>

using physics::HitboxHit;
using physics::HitscanHit;
using physics::resolveHitscan;
using physics::resolveHitscanHitbox;

namespace systems
{

inline bool combatLogEnabled()
{
    static const bool enabled = [] {
        const char* env = std::getenv("GROUP2_COMBAT_LOG");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    return enabled;
}

/// @brief Apply weapon slot switch from player input.
/// @param input   Current input snapshot.
/// @param weapon  Weapon state (modified in place).
void handleSwitch(const InputSnapshot& input, WeaponState& weapon)
{
    if (input.switchToPrimary && weapon.current != WeaponSlot::PRIMARY) {
        auto& gun = getEquippedGun(weapon);
        gun.isReloading = false;
        gun.reloadTime = 0.0f;
        gun.recoilHeat = 0.0f;
        gun.recoilIdleTime = 0.0f;
        weapon.current = WeaponSlot::PRIMARY;
    } else if (input.switchToSecondary && weapon.current != WeaponSlot::SECONDARY) {
        auto& gun = getEquippedGun(weapon);
        gun.isReloading = false;
        gun.reloadTime = 0.0f;
        gun.recoilHeat = 0.0f;
        gun.recoilIdleTime = 0.0f;
        weapon.current = WeaponSlot::SECONDARY;
    }
}

/// @brief Tick fire cooldowns for all weapon slots.
/// @param weapon  Weapon state (modified in place).
/// @param dt      Fixed physics delta time in seconds.
inline void handleCooldown(WeaponState& weapon, float dt)
{
    for (auto& gun : weapon.slots) {
        gun.fireCooldown = std::max(0.0f, gun.fireCooldown - dt);

        if (gun.isReloading) {
            gun.reloadTime -= dt;
            if (gun.reloadTime <= 0.0f) {
                const WeaponConfig& config = getWeaponConfig(gun.type);
                int reloadAmount = config.magazineSize - gun.currentMagAmmo;
                if (gun.totalAmmo >= reloadAmount) {
                    gun.currentMagAmmo += reloadAmount;
                    gun.totalAmmo -= reloadAmount;
                } else {
                    gun.currentMagAmmo += gun.totalAmmo;
                    gun.totalAmmo = 0;
                }
                gun.isReloading = false;
                gun.reloadTime = 0.0f;
            }
        }
    }
}

inline void handleGrenadeCooldown(GrenadeState& grenades, float dt)
{
    grenades.cooldown = std::max(0.0f, grenades.cooldown - dt);
}

/// @brief Reload the gun's magazine from reserve ammo.
/// @param gun  Gun instance to reload (modified in place).
inline void handleReload(GunInstance& gun)
{
    const WeaponConfig& config = getWeaponConfig(gun.type);
    if (!gun.isReloading && gun.totalAmmo > 0 && gun.currentMagAmmo < config.magazineSize) {
        gun.isReloading = true;
        gun.reloadTime = config.reloadTime;
    }
}

/// @brief Consume one round from the magazine; auto-reload if empty.
/// @param gun  Gun instance to consume ammo from (modified in place).
/// @return True if a round was consumed, false if the gun is empty.
inline bool handleAmmo(GunInstance& gun)
{
    if (gun.currentMagAmmo <= 0) {
        handleReload(gun);
        return false;
    }

    --gun.currentMagAmmo;
    return true;
}

/// @brief Compute the player's 3D view direction from yaw and pitch angles.
/// @param yaw    Horizontal angle (radians).
/// @param pitch  Vertical angle (radians, positive = down).
/// @return Normalized forward direction vector.
inline glm::vec3 viewForward(float yaw, float pitch)
{
    // Must match client camera convention:
    //   X = sin(yaw) * cos(pitch)
    //   Y = -sin(pitch)
    //   Z = cos(yaw) * cos(pitch)
    const float cp = std::cos(pitch);
    return glm::normalize(glm::vec3{
        std::sin(yaw) * cp,
        -std::sin(pitch),
        std::cos(yaw) * cp,
    });
}

/// @brief Offset the muzzle origin from the eye position for tracer visuals.
///
/// Shifts the origin right and down from eye, slightly forward, so tracers
/// don't originate from the center of the screen.
/// @param eye             Eye position (camera origin).
/// @param direction       Normalized view direction.
/// @param gravityFlipped  When true, negate the horizontal offset so the
///                        tracer originates from the viewmodel side.  The
///                        viewmodel stays on screen-right, but the 180°
///                        camera roll reverses world-left/right — so
///                        world-right (the normal offset) appears on the
///                        wrong side of the screen.
/// @return Offset muzzle position in world space.
inline glm::vec3 muzzleOrigin(glm::vec3 eye, glm::vec3 direction, bool gravityFlipped = false)
{
    constexpr glm::vec3 k_worldUp{0.0f, 1.0f, 0.0f};
    glm::vec3 right = glm::cross(direction, k_worldUp);
    if (glm::dot(right, right) < physics::k_parallelEpsilon) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        right = glm::normalize(right);
    }

    // Flip only the horizontal offset — the viewmodel is still on
    // screen-right but the 180° roll maps that to the opposite
    // world-space direction.  The vertical ("down from eye") stays
    // the same because the viewmodel rendering already compensates.
    if (gravityFlipped)
        right = -right;

    const glm::vec3 up = glm::normalize(glm::cross(right, direction));
    return eye + right * 15.0f - up * 8.0f + direction * 5.0f;
}

// PR-18b: log a single hitscan shot's resolution to the optional
// server-side shot-resolution CSV.  Reads shooter's ClientId out of
// the registry (the server stamps one on every replicated player at
// connect-time).  Hit target's ClientId is resolved from `hit.entity`
// or set to `k_missClientId` for misses.  No-op on client TUs (the
// `#if GROUP2_HAS_SHOTLOG` guard collapses to a stubbed body).
//
// PR-22 (netsync): also records the shot ray (origin, direction), the
// shooter's `LagCompTarget` (RTT + rewind ticks), and the hit target's
// rewound vs current position.  Caller MUST have an active
// `RewindHitboxesGuard` so reading the hit target's `HitboxInstance::
// capsules` returns the historical (rewound) sample — exactly what
// the raycast just hit.  `Position.value` is unchanged by the guard
// and gives the live (current) foot position.
inline void logShot(Registry& registry,
                    entt::entity shooter,
                    std::uint32_t shotInputTick,
                    const glm::vec3& origin,
                    const glm::vec3& direction,
                    const physics::HitboxHit& hit)
{
#if GROUP2_HAS_SHOTLOG
    group2::perf::shotlog::ShotResolution row;
    row.shotInputTick = shotInputTick;

    if (const auto* sid = registry.try_get<ClientId>(shooter))
        row.shooterClientId = static_cast<std::uint16_t>(sid->value);

    row.hitX = hit.point.x;
    row.hitY = hit.point.y;
    row.hitZ = hit.point.z;
    row.hitRegion = static_cast<int>(hit.region);

    row.originX = origin.x;
    row.originY = origin.y;
    row.originZ = origin.z;
    row.dirX = direction.x;
    row.dirY = direction.y;
    row.dirZ = direction.z;

    if (const auto* lagComp = registry.try_get<LagCompTarget>(shooter)) {
        row.shooterRttMs = lagComp->rttMs;
        row.lagCompTicks = lagComp->lagTicks;
    }

    if (hit.entity != entt::null && registry.valid(hit.entity)) {
        if (const auto* tid = registry.try_get<ClientId>(hit.entity))
            row.hitClientId = static_cast<std::uint16_t>(tid->value);

        // Rewound centre: while the guard is active, `capsules[0]` is
        // the historical sample.  Capsule index 0 is the body capsule
        // for our rig (see Hitbox.hpp); its midpoint is a stable
        // rewound-pose anchor.
        if (const auto* hb = registry.try_get<HitboxInstance>(hit.entity); hb != nullptr && !hb->capsules.empty()) {
            const auto& cap0 = hb->capsules[0];
            const glm::vec3 centroid = (cap0.pointA + cap0.pointB) * 0.5f;
            row.hitTargetRewoundX = centroid.x;
            row.hitTargetRewoundY = centroid.y;
            row.hitTargetRewoundZ = centroid.z;
        }

        // Current centre: `Position.value` is unchanged by the rewind
        // guard.  This is the live foot position at server-now-tick;
        // the analyzer subtracts capsule-midpoint vertical offset
        // when computing drift.
        if (const auto* tpos = registry.try_get<Position>(hit.entity)) {
            row.hitTargetCurrentX = tpos->value.x;
            row.hitTargetCurrentY = tpos->value.y;
            row.hitTargetCurrentZ = tpos->value.z;
        }
    }

    // PR-27: client-asserted animation-state telemetry.  ServerGame
    // stashes paired SHOT_INTENTs on the shooter as `PendingShotIntent`
    // before runWeapon; here we read it, find the matching anim
    // history sample on the target at the rewound tick, and compute
    // the delta.  Numbers go to `server_shots.csv` for telemetry only
    // — PR-27b will use the delta to gate accepting the client's view.
    if (const auto* intent = registry.try_get<PendingShotIntent>(shooter); intent != nullptr && intent->received) {
        row.clientIntentReceived = 1;
        row.clientIntentTargetClientId = intent->targetClientId;

        // Compute delta only when both ids match — comparing client's
        // anim of target X to server's anim of target Y is meaningless.
        if (row.hitClientId == intent->targetClientId &&
            intent->targetClientId != group2::perf::shotlog::k_missClientId)
        {
            // Look up server's HISTORICAL anim state for this target
            // at the rewound tick.  `LagCompTarget.targetServerTick`
            // tells us which historical sample to pull from the ring;
            // 0 means "no rewind", in which case the LIVE anim state
            // on the target is the right comparison.
            AnimSnapshot serverAnim{};
            bool serverAnimResolved = false;
            if (const auto* hist = registry.try_get<HitboxHistory>(hit.entity)) {
                std::uint32_t targetTick = 0;
                if (const auto* lc = registry.try_get<LagCompTarget>(shooter))
                    targetTick = lc->targetServerTick;
                if (targetTick != 0) {
                    const HitboxHistorySample* best = nullptr;
                    for (std::size_t i = 0; i < hist->count; ++i) {
                        const auto& s = hist->ring[i];
                        if (s.tick == 0 || s.tick > targetTick)
                            continue;
                        if (best == nullptr || s.tick > best->tick)
                            best = &s;
                    }
                    if (best != nullptr) {
                        serverAnim = best->anim;
                        serverAnimResolved = true;
                    }
                }
            }
            // Fallback to LIVE anim state when no historical sample
            // exists (just-spawned target / no rewind requested).
            if (!serverAnimResolved) {
                if (const auto* live = registry.try_get<AnimSnapshot>(hit.entity))
                    serverAnim = *live;
            }
            row.animStateDelta = anim_snapshot::delta(intent->targetAnim, serverAnim);
        }
    }

    group2::perf::shotlog::recordShotResolution(row);
#else
    (void)registry;
    (void)shooter;
    (void)shotInputTick;
    (void)origin;
    (void)direction;
    (void)hit;
#endif
}

// PR-20: capture the post-rewind state for the lag-comp debug
// visualizer.  Pushes one row into `*out` carrying:
//   - the shooter's own ClientId (so ServerGame can address the
//     unicast SHOT_DEBUG_REPORT packet),
//   - shot ray (origin, dir, range),
//   - hit target + hit point + region,
//   - the REWOUND capsule list of every player entity in lag-comp
//     range (the same capsules `resolveHitscanHitbox` just raycast).
//
// Caller MUST hold the `RewindHitboxesGuard` active around the call
// — that's what makes `HitboxInstance::capsules` carry historical
// data instead of live data.  No-op when `out == nullptr` (client
// TU running WeaponSystem for prediction).
inline void captureShotDebug(Registry& registry,
                             entt::entity shooter,
                             std::uint32_t shotInputTick,
                             const glm::vec3& origin,
                             const glm::vec3& direction,
                             float range,
                             const physics::HitboxHit& hit,
                             std::vector<net::shotdebug::ShotDebugCapture>* out)
{
    if (out == nullptr)
        return;

    // Need a real ClientId on the shooter — bots / map entities
    // without one can't be addressed for the unicast reply.
    const auto* shooterCid = registry.try_get<ClientId>(shooter);
    if (shooterCid == nullptr)
        return;

    net::shotdebug::ShotDebugCapture cap;
    cap.shooterClientId = static_cast<std::uint16_t>(shooterCid->value);
    cap.shotInputTick = shotInputTick;
    cap.origin = origin;
    cap.direction = direction;
    cap.range = range;
    cap.hitTargetClientId = net::shotdebug::k_missClientId;
    cap.hitRegion = 0;
    cap.hitPoint = hit.point;
    if (hit.entity != entt::null && registry.valid(hit.entity)) {
        if (const auto* tid = registry.try_get<ClientId>(hit.entity)) {
            cap.hitTargetClientId = static_cast<std::uint16_t>(tid->value);
            cap.hitRegion = static_cast<std::uint8_t>(hit.region);
        }
    }

    // Snapshot the CURRENT (rewound) capsules of every player whose
    // capsule-derived AABB — DILATED by `shotDebugAimMargin` on each
    // axis — intersects the shot ray.  PR-26 originally used the
    // exact capsule AABB (the same broad-phase as the raycast); user
    // feedback was that misses dropped the target wireframe entirely,
    // even when the shot was clearly aimed at them ("ray went 5 cm
    // above the head and I lost the wireframe").  The aim margin
    // turns the filter into "show whoever I was approximately
    // shooting at" rather than "show whoever the ray geometrically
    // hit" — clutter still gone (distant unrelated enemies skipped),
    // but near-miss targets stay visible so the user can see WHY the
    // shot missed (rewind lag-comp drift, animation pose, etc).
    constexpr float shotDebugAimMargin = 50.0f;
    auto view = registry.view<HitboxInstance, ClientId>();
    cap.targets.reserve(view.size_hint());
    for (const auto e : view) {
        if (e == shooter)
            continue;
        const auto& hb = view.get<HitboxInstance>(e);
        if (hb.capsules.empty())
            continue;

        glm::vec3 boundsMin{std::numeric_limits<float>::max()};
        glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};
        for (const auto& c : hb.capsules) {
            const glm::vec3 capRadius{c.radius + shotDebugAimMargin};
            boundsMin = glm::min(boundsMin, glm::min(c.pointA, c.pointB) - capRadius);
            boundsMax = glm::max(boundsMax, glm::max(c.pointA, c.pointB) + capRadius);
        }
        const physics::WorldAABB bounds{.min = boundsMin, .max = boundsMax};
        float aabbDist = range;
        glm::vec3 aabbNormal{0.0f};
        if (!physics::raycastAABB(origin, direction, bounds, range, aabbDist, aabbNormal))
            continue;

        const auto& cid = view.get<ClientId>(e);
        net::shotdebug::ShotDebugCapture::Target tgt;
        tgt.clientId = static_cast<std::uint16_t>(cid.value);
        tgt.capsules = hb.capsules; // copy while rewound
        cap.targets.push_back(std::move(tgt));
    }

    out->push_back(std::move(cap));
}

/// @brief Spawn a grenade projectile from the player's eye position.
///
/// Computes a throw direction by rotating the eye direction upward by the
/// configured (small) pitch offset, then adds the thrower's velocity so a
/// grenade thrown on the move inherits the player's momentum (Halo-style):
/// moving forward extends the throw, backpedaling shortens it.
/// Copies all flight-relevant fields from the grenade's GrenadeConfig into
/// the new Projectile entity so CollisionSystem can dispatch on them.
static void spawnGrenade(Registry& registry,
                         entt::entity shooter,
                         WeaponType type,
                         glm::vec3 muzzle,
                         glm::vec3 eyeDir,
                         glm::vec3 eyeRight,
                         glm::vec3 throwerVel)
{
    const GrenadeConfig& cfg = getGrenadeConfig(type);

    // Rotate eyeDir upward around eyeRight by `throwPitchOffset` rad so the throw
    // arcs slightly above the crosshair. Positive angle about eyeRight pitches up.
    const glm::vec3 throwDir = glm::normalize(glm::angleAxis(cfg.throwPitchOffset, eyeRight) * eyeDir);

    const entt::entity proj = registry.create();
    registry.emplace<Projectile>(proj,
                                 Projectile{
                                     .type = type,
                                     .damage = cfg.damage,
                                     .owner = shooter,
                                     .explosive = false, // grenades route via fuse / impact, not the rocket path
                                     .currentLifeTime = 0.0f,
                                     // Sticky grenades start with no fuse — CollisionSystem arms it on first
                                     // surface hit. Non-sticky grenades (HE w/ fuseTime=3.0; Molotov w/ fuseTime=-1 for
                                     // impact-detonate) use the config value directly.
                                     .fuseTimer = cfg.sticky ? -1.0f : cfg.fuseTime,
                                     .bounceRestitution = cfg.bounceRestitution,
                                     .sticky = cfg.sticky,
                                     .tint = cfg.tint,
                                 });
    registry.emplace<Position>(proj, Position{.value = muzzle});
    registry.emplace<Velocity>(proj, Velocity{.value = throwDir * cfg.throwSpeed + throwerVel});
    registry.emplace<CollisionShape>(proj, CollisionShape{.halfExtents = {5.0f, 5.0f, 5.0f}});
}

/// @brief Find the next grenade type index (wrapping in direction @p dir, +1 or
/// -1) that still has ammo, starting the search one step away from @p start.
/// Returns @p start when no other type has ammo (including when @p start itself
/// is the only one with ammo, or none do).
inline int nextGrenadeWithAmmo(const GrenadeState& grenades, int start, int dir)
{
    const int count = static_cast<int>(kGrenadeTypeCount);
    for (int step = 1; step <= count; ++step) {
        const int idx = (((start + dir * step) % count) + count) % count;
        if (grenades.ammo[static_cast<std::size_t>(idx)] > 0) {
            return idx;
        }
    }
    return start;
}

inline void handleGrenadeInput(Registry& registry,
                               entt::entity shooter,
                               InputSnapshot& input,
                               const Position& pos,
                               const CollisionShape& shape,
                               GrenadeState& grenades,
                               bool gravityFlipped)
{
    // Cycle the selected grenade type, skipping types the player has no ammo
    // for. next/prev are edge-pulsed once per input and consumed here so the
    // selection advances exactly one held-ammo step. If no other type has ammo
    // the selection stays put.
    if (input.grenadeCycleNext || input.grenadeCyclePrev) {
        const int dir = input.grenadeCyclePrev ? -1 : 1;
        const int start = static_cast<int>(grenadeTypeIndex(grenades.selected));
        grenades.selected = grenadeTypeAt(static_cast<std::size_t>(nextGrenadeWithAmmo(grenades, start, dir)));
        input.grenadeCycleNext = false;
        input.grenadeCyclePrev = false;
    }

    if (!input.throwGrenade) {
        return;
    }
    input.throwGrenade = false;

    const WeaponType type = grenades.selected;
    if (!isGrenadeType(type) || grenades.cooldown > 0.0f || grenadeAmmo(grenades, type) <= 0) {
        return;
    }

    const float eyeDirSign = gravityFlipped ? -1.0f : 1.0f;
    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f * eyeDirSign, 0.0f};
    const glm::vec3 direction = viewForward(input.yaw, input.pitch);
    const glm::vec3 muzzle = muzzleOrigin(eye, direction, gravityFlipped);

    glm::vec3 eyeRight = glm::cross(direction, glm::vec3{0.0f, 1.0f, 0.0f});
    const float eyeRightLen2 = glm::dot(eyeRight, eyeRight);
    if (eyeRightLen2 < physics::k_parallelEpsilon) {
        eyeRight = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        eyeRight = eyeRight * (1.0f / std::sqrt(eyeRightLen2));
    }

    const Velocity* throwerVel = registry.try_get<Velocity>(shooter);
    const glm::vec3 inheritVel = (throwerVel != nullptr) ? throwerVel->value : glm::vec3{0.0f};

    spawnGrenade(registry, shooter, type, muzzle, direction, eyeRight, inheritVel);
    grenades.cooldown = getGrenadeConfig(type).throwCooldown;
    --grenadeAmmo(grenades, type);

    // Auto-cycle to the next grenade type that still has ammo when this one runs
    // dry, so the player is never left holding an empty type.
    if (grenadeAmmo(grenades, type) <= 0) {
        const int start = static_cast<int>(grenadeTypeIndex(grenades.selected));
        grenades.selected = grenadeTypeAt(static_cast<std::size_t>(nextGrenadeWithAmmo(grenades, start, 1)));
    }
}

inline void
handleScope(Registry& registry, entt::entity shooter, const InputSnapshot& input, WeaponState& weapon, float dt)
{
    GunInstance& gun = getEquippedGun(weapon);
    const WeaponConfig& config = getWeaponConfig(gun.type);

    if (!config.isCharge || gun.fireCooldown > 0.0f) {
        return;
    }

    if (gun.isReloading) {
        gun.chargeTime = 0; // lose change if you reload
        return;
    }

    if (input.scoped) {
        gun.chargeTime = std::min((gun.chargeTime + dt), config.maxChargeTime);
    } else {
        gun.chargeTime = 0;
    }
}

/// @brief Result of an auto-lock cone scan for a Tesla-style beam.
struct BeamLockResult
{
    entt::entity target{entt::null};           ///< Best locked enemy, or null if none in cone.
    glm::vec3 point{0.0f};                     ///< Aim point on the target (centre of mass) for VFX + damage.
    BodyRegion region{BodyRegion::UpperTorso}; ///< Region credited for the hit (centre mass = torso).
};

/// @brief Pick the enemy closest to the crosshair inside the lock-on cone.
///
/// Scans every other player's hitbox capsules. A candidate qualifies when its
/// centre of mass is within `maxRange`, inside the cone of half-angle
/// `coneHalfAngleDeg` around `viewDir`, and has clear line-of-sight from `eye`
/// (no world geometry between the shooter and the target). Among qualifying
/// candidates the one with the smallest angular deviation from the crosshair
/// wins. Must be called while any lag-compensation rewind guard is in scope so
/// the capsules reflect the attacker's screen-time positions.
inline BeamLockResult findBeamLockTarget(
    Registry& registry, entt::entity shooter, glm::vec3 eye, glm::vec3 viewDir, const WeaponConfig& config)
{
    BeamLockResult result;
    const float maxRange = (config.maxRange > 0.0f) ? config.maxRange : physics::k_hitscanRange;
    constexpr float kDegToRad = 3.14159265358979f / 180.0f;
    const float minCos = std::cos(config.coneHalfAngleDeg * kDegToRad);
    float bestCos = minCos; // candidate must beat the cone edge to qualify

    registry.view<Position, CollisionShape, HitboxInstance>().each(
        [&](entt::entity e, const Position&, const CollisionShape&, const HitboxInstance& hb) {
            if (e == shooter || hb.capsules.empty())
                return;

            // Centre of mass = midpoint of the capsule-derived AABB.
            glm::vec3 lo{std::numeric_limits<float>::max()};
            glm::vec3 hi{std::numeric_limits<float>::lowest()};
            for (const WorldCapsule& cap : hb.capsules) {
                lo = glm::min(lo, glm::min(cap.pointA, cap.pointB) - glm::vec3{cap.radius});
                hi = glm::max(hi, glm::max(cap.pointA, cap.pointB) + glm::vec3{cap.radius});
            }
            const glm::vec3 centre = (lo + hi) * 0.5f;

            const glm::vec3 to = centre - eye;
            const float dist = glm::length(to);
            if (dist < 1e-3f || dist > maxRange)
                return;

            const glm::vec3 dir = to / dist;
            const float c = glm::dot(dir, viewDir);
            if (c <= bestCos)
                return; // outside cone, or not closer to crosshair than the current best

            // Line-of-sight: reject if world geometry sits between eye and target.
            const physics::HitscanHit worldHit = physics::raycastWorld(eye, dir, physics::activeWorld());
            if (worldHit.hit && worldHit.distance < dist - 1.0f)
                return;

            bestCos = c;
            result.target = e;
            result.point = centre;
            result.region = BodyRegion::UpperTorso;
        });

    return result;
}

/// @brief Process fire input: hitscan raycasts, beam weapons, charge shots, and projectiles.
///
/// Handles three weapon archetypes:
///  - **Beam** — continuous DPS drain while held, capsule raycast each tick.
///  - **Charge** — accumulates chargeTime while held, fires on release.
///  - **Discrete** — standard per-click hitscan or projectile spawn.
///
/// Emits NetParticleEvent entries for tracer/impact effects and applies damage
/// through applyDamage() which may trigger kill events.
///
/// @param registry        The ECS registry.
/// @param shooter         Entity that is firing.
/// @param input           Current input snapshot.
/// @param pos             Shooter position.
/// @param shape           Shooter collision shape (for eye height).
/// @param weapon          Shooter weapon state (modified in place).
/// @param gravityFlipped  True when the shooter's gravity is inverted.
/// @param dt              Fixed physics delta time in seconds.
/// @param outParticles    Accumulates particle events for network broadcast.
/// @param killEvents      Accumulates kill events for network broadcast.
/// @param outShotDebug    PR-20: optional server-side debug capture sink.
///                        When non-null, each hitscan-fire path pushes one
///                        `ShotDebugCapture` row while `RewindHitboxesGuard`
///                        is still active, so the captured `targets[*].
///                        capsules` reflect the historical sample the
///                        server actually raycast against.
inline void handleFire(Registry& registry,
                       entt::entity shooter,
                       const InputSnapshot& input,
                       const Position& pos,
                       const CollisionShape& shape,
                       WeaponState& weapon,
                       bool gravityFlipped,
                       bool grenadeThrowActive,
                       float dt,
                       std::vector<NetParticleEvent>& outParticles,
                       std::vector<NetKillEvent>& killEvents,
                       std::vector<net::shotdebug::ShotDebugCapture>* outShotDebug)
{
    GunInstance& gun = getEquippedGun(weapon);
    const WeaponConfig& config = getWeaponConfig(gun.type);

    // Reloading and the grenade-throw wind-up both lock out firing.
    if (gun.isReloading || grenadeThrowActive) {
        gun.recoilHeat = 0.0f;
        gun.recoilIdleTime = 0.0f;
        if (auto* beam = registry.try_get<BeamState>(shooter)) {
            beam->active = false; // turn off beam visuals during reload / throw
        }
        if (auto* lockState = registry.try_get<BeamLockState>(shooter)) {
            lockState->target = entt::null; // reload breaks the lock / resets ramp
            lockState->duration = 0.0f;
        }
        return;
    }

    // ── Beam weapon path ──
    if (config.isBeam) {
        auto& beam = registry.get_or_emplace<BeamState>(shooter);

        if (!input.shooting || gun.currentMagAmmo <= 0) {
            beam.active = false;
            // Releasing the trigger breaks the lock: the ramp restarts from base
            // next time fire begins (strict, no carry-over).
            if (auto* lockState = registry.try_get<BeamLockState>(shooter)) {
                lockState->target = entt::null;
                lockState->duration = 0.0f;
            }
            return;
        }

        // Drain ammo over time (fractional accumulation).
        gun.fireCooldown += config.ammoPerSecond * dt; // repurpose cooldown as drain accumulator
        if (gun.fireCooldown >= 1.0f) {
            const int drain = static_cast<int>(gun.fireCooldown);
            gun.currentMagAmmo = std::max(0, gun.currentMagAmmo - drain);
            gun.fireCooldown -= static_cast<float>(drain);
        }

        // Beam origin (eye) + aim direction.
        const float eyeDir = gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f * eyeDir, 0.0f};
        const glm::vec3 direction = viewForward(input.yaw, input.pitch);

        // ── Auto-lock cone beam (Tesla Cannon) ──
        // Pick the enemy closest to the crosshair inside the lock-on cone and
        // within range, then apply ramping damage that resets when the lock
        // breaks. Shields resist (energy-vs-energy), forcing a finisher role.
        if (config.autoLockBeam) {
            const float maxRange = (config.maxRange > 0.0f) ? config.maxRange : physics::k_hitscanRange;
            // Rewind so the cone scan sees the attacker's screen-time positions.
            const auto rewindGuard = systems::rewindHitboxes(registry, shooter, &eye, &direction, maxRange, 0.0f);
            const BeamLockResult lock = findBeamLockTarget(registry, shooter, eye, direction, config);

            auto& lockState = registry.get_or_emplace<BeamLockState>(shooter);
            beam.active = true;
            beam.type = gun.type;
            beam.origin = eye;

            if (lock.target != entt::null && registry.valid(lock.target)) {
                // Maintain or (re)acquire: any target change resets the ramp.
                if (lock.target == lockState.target) {
                    lockState.duration += dt;
                } else {
                    lockState.target = lock.target;
                    lockState.duration = 0.0f;
                }

                // Ramp DPS from base (`dps`) to `dpsMax` over `dpsRampTime`.
                float dps = config.dps;
                if (config.dpsMax > config.dps && config.dpsRampTime > 0.0f) {
                    const float r = std::clamp(lockState.duration / config.dpsRampTime, 0.0f, 1.0f);
                    dps = config.dps + (config.dpsMax - config.dps) * r;
                }
                const float multiplier = defaultDamageProfile().multipliers[static_cast<size_t>(lock.region)];
                applyDamage(dps * dt * multiplier,
                            lock.target,
                            shooter,
                            registry,
                            killEvents,
                            lock.region,
                            config.shieldDamageMultiplier);
                applyBulletSlow(lock.target, registry);

                beam.hitPoint = lock.point;
            } else {
                // No valid lock: ramp resets; beam sprays forward to the cap.
                lockState.target = entt::null;
                lockState.duration = 0.0f;
                beam.hitPoint = eye + direction * maxRange;
            }
            return;
        }

        // ── Legacy straight-ray beam path ──
        // Phase 6 lag-compensated hitscan. The guard reads
        // `LagCompTarget` off `shooter` (set each tick by the server's
        // lag-comp scheduler from this client's reported RTT), swaps
        // every other player's `HitboxInstance::capsules` for the
        // historical sample matching the attacker's screen at fire
        // time, and restores live capsules on scope exit. No-op for
        // shooters with no `LagCompTarget` (e.g. the client TU, where
        // this same WeaponSystem.cpp is compiled but no entity ever
        // gets the component).
        //
        // PR-5 (server-perf): pass the ray to skip rewind work for
        // players whose AABB doesn't intersect the shot. Cuts O(N)
        // per shot to O(candidates).
        const auto rewindGuard =
            systems::rewindHitboxes(registry, shooter, &eye, &direction, physics::k_hitscanRange, config.hitscanRadius);
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction, config.hitscanRadius);

        // PR-18b: log to server-side shot-resolution CSV (no-op when
        // env unset).  Beam path: every active-fire tick records.
        logShot(registry, shooter, input.tick, eye, direction, hit);
        // PR-20: capture rewound state for the live debug visualizer
        // (no-op on client TU and when no shooter ClientId).  Must
        // happen WHILE rewindGuard is still in scope.
        captureShotDebug(registry, shooter, input.tick, eye, direction, physics::k_hitscanRange, hit, outShotDebug);

        // Apply DPS-based damage with body-region multiplier.
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            const float multiplier = defaultDamageProfile().multipliers[static_cast<size_t>(hit.region)];
            applyDamage(config.dps * dt * multiplier, hit.entity, shooter, registry, killEvents, hit.region);
            applyBulletSlow(hit.entity, registry);
        }

        // Update BeamState (synced to clients via registry snapshot).
        beam.active = true;
        beam.type = gun.type;
        beam.origin = eye;
        beam.hitPoint = hit.point;
        return;
    }

    // ── Discrete weapon path ──
    if (!input.shooting) {
        gun.recoilIdleTime += dt;
        if (gun.recoilIdleTime > 0.2f) {
            gun.recoilHeat -= config.recoilRecovery * dt;
            if (gun.recoilHeat <= float(config.recoilFreeShots))
                gun.recoilHeat = 0.0f;
        }
        // Semi-auto: trigger release re-arms the next shot.
        gun.firedSinceTriggerPress = false;
        return;
    }
    gun.recoilIdleTime = 0.0f;

    if (gun.fireCooldown > 0.0f) {
        return;
    }

    // Semi-auto weapons (e.g. pump shotgun) must release+repress the trigger
    // between shots. Holding the trigger only fires once per press.
    if (config.semiAuto && gun.firedSinceTriggerPress) {
        return;
    }

    if (!handleAmmo(gun)) {
        return;
    }

    gun.fireCooldown = config.fireCooldown;
    gun.recoilHeat += 1.0f;
    gun.firedSinceTriggerPress = true;

    // Railgun handling
    if (config.isCharge) {

        const float eyeDirCharge = gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f * eyeDirCharge, 0.0f};
        const glm::vec3 direction = viewForward(input.yaw, input.pitch);
        // Phase 6 lag-compensated hitscan (see beam path for details).
        // PR-5: ray-filtered rewind, see beam path.
        const auto rewindGuard =
            systems::rewindHitboxes(registry, shooter, &eye, &direction, physics::k_hitscanRange, config.hitscanRadius);
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction, config.hitscanRadius);

        // PR-18b: log to server-side shot-resolution CSV.  Charge
        // path: one log row per release-fire.
        logShot(registry, shooter, input.tick, eye, direction, hit);
        // PR-20: capture rewound state for the live debug visualizer.
        captureShotDebug(registry, shooter, input.tick, eye, direction, physics::k_hitscanRange, hit, outShotDebug);

        // Snapshot armor before damage for shield-break detection.
        float chargeArmorBefore = 0.f;
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            if (const auto* hp = registry.try_get<Health>(hit.entity))
                chargeArmorBefore = hp->armor;
        }

        SDL_Log("CURRENT CHARGE: %f", gun.chargeTime);
        // Charge damage with body-region multiplier.
        float chargeDealtDamage = 0.f;
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            const float multiplier = defaultDamageProfile().multipliers[static_cast<size_t>(hit.region)];
            gun.chargeTime = std::min(gun.chargeTime, config.maxChargeTime);

            auto outGoingDamage =
                (config.damage + (config.chargeDamage * (gun.chargeTime / config.maxChargeTime))) * multiplier;
            chargeDealtDamage = applyDamage(outGoingDamage, hit.entity, shooter, registry, killEvents, hit.region);
            applyBulletSlow(hit.entity, registry);
            if (hit.region == BodyRegion::Head && combatLogEnabled()) {
                SDL_Log("[weapon] HEADSHOT! charge weapon hit %d in head for %.0f damage",
                        static_cast<int>(hit.entity),
                        static_cast<double>(chargeDealtDamage));
            }
        }

        gun.chargeTime = 0.0f;

        bool chargeShieldBroke = false;
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            if (const auto* hp = registry.try_get<Health>(hit.entity))
                chargeShieldBroke = (chargeArmorBefore > 0.f && hp->armor <= 0.f);
        }

        // Emit particle events (beam + impact).
        {
            NetParticleEvent tracerEvt;
            tracerEvt.source = shooter;
            tracerEvt.weaponType = gun.type;
            tracerEvt.effectType = ParticleEffectType::HitscanBeam;
            tracerEvt.pos1 = eye;
            tracerEvt.pos2 = hit.point;
            outParticles.push_back(tracerEvt);
        }
        {
            NetParticleEvent impactEvt;
            impactEvt.source = shooter;
            impactEvt.effectType = ParticleEffectType::Impact;
            impactEvt.weaponType = gun.type;
            impactEvt.surfaceType = (hit.entity != entt::null) ? SurfaceType::Flesh : SurfaceType::Concrete;
            impactEvt.headshot = (hit.region == BodyRegion::Head) ? uint8_t{1} : uint8_t{0};
            impactEvt.shieldBreak = chargeShieldBroke ? uint8_t{1} : uint8_t{0};
            impactEvt.hadArmor = (chargeArmorBefore > 0.f) ? uint8_t{1} : uint8_t{0};
            impactEvt.damage = chargeDealtDamage;
            impactEvt.target = hit.entity;
            impactEvt.pos1 = hit.point;
            impactEvt.pos2 = hit.normal;
            outParticles.push_back(impactEvt);
        }

        return;
    }

    const float eyeDirDiscrete = gravityFlipped ? -1.0f : 1.0f;
    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f * eyeDirDiscrete, 0.0f};
    const glm::vec3 direction = viewForward(input.yaw, input.pitch);
    const glm::vec3 muzzle = muzzleOrigin(eye, direction, gravityFlipped);

    if (config.hitscan) {
        // Phase 6 lag-compensated hitscan (see beam path for details).
        // PR-5: ray-filtered rewind. Rewind uses the central aim direction; all
        // pellets in a shotgun blast share the same rewound state.
        const auto rewindGuard =
            systems::rewindHitboxes(registry, shooter, &eye, &direction, physics::k_hitscanRange, config.hitscanRadius);

        // Per-pellet resolver: runs the raycast, applies damage, emits the
        // tracer + impact events. Used once for normal hitscan and N times for
        // shotgun in a star spread.
        const auto resolvePellet = [&](const glm::vec3& pelletDir, bool logCenter) {
            const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, pelletDir, config.hitscanRadius);

            if (logCenter) {
                logShot(registry, shooter, input.tick, eye, pelletDir, hit);
                captureShotDebug(
                    registry, shooter, input.tick, eye, pelletDir, physics::k_hitscanRange, hit, outShotDebug);
            }

            float armorBefore = 0.f;
            if (hit.entity != entt::null && registry.valid(hit.entity)) {
                if (const auto* hp = registry.try_get<Health>(hit.entity))
                    armorBefore = hp->armor;
            }

            float dealtDamage = 0.f;
            if (hit.entity != entt::null && registry.valid(hit.entity)) {
                const float multiplier = defaultDamageProfile().multipliers[static_cast<size_t>(hit.region)];
                dealtDamage =
                    applyDamage(config.damage * multiplier, hit.entity, shooter, registry, killEvents, hit.region);
                applyBulletSlow(hit.entity, registry);
                if (hit.region == BodyRegion::Head && combatLogEnabled()) {
                    SDL_Log("[weapon] HEADSHOT! %d hit %d for %.0f damage (base %.0f x %.1f)",
                            static_cast<int>(shooter),
                            static_cast<int>(hit.entity),
                            static_cast<double>(dealtDamage),
                            static_cast<double>(config.damage),
                            static_cast<double>(multiplier));
                }
            }

            bool shieldBroke = false;
            if (hit.entity != entt::null && registry.valid(hit.entity)) {
                if (const auto* hp = registry.try_get<Health>(hit.entity))
                    shieldBroke = (armorBefore > 0.f && hp->armor <= 0.f);
            }

            // 1) Tracer/beam from muzzle to hit point.
            {
                NetParticleEvent tracerEvt;
                tracerEvt.source = shooter;
                tracerEvt.weaponType = gun.type;
                if (gun.type == WeaponType::RailGun || gun.type == WeaponType::EnergyGun) {
                    tracerEvt.effectType = ParticleEffectType::HitscanBeam;
                    tracerEvt.pos1 = muzzle;
                    tracerEvt.pos2 = hit.point;
                } else {
                    tracerEvt.effectType = ParticleEffectType::BulletTracer;
                    tracerEvt.pos1 = muzzle;
                    tracerEvt.pos2 = glm::normalize(hit.point - muzzle);
                    tracerEvt.param = hit.distance;
                }
                outParticles.push_back(tracerEvt);
            }
            // 2) Impact effect at hit location.
            {
                NetParticleEvent impactEvt;
                impactEvt.source = shooter;
                impactEvt.effectType = ParticleEffectType::Impact;
                impactEvt.weaponType = gun.type;
                impactEvt.surfaceType = (hit.entity != entt::null) ? SurfaceType::Flesh : SurfaceType::Concrete;
                impactEvt.headshot = (hit.region == BodyRegion::Head) ? uint8_t{1} : uint8_t{0};
                impactEvt.shieldBreak = shieldBroke ? uint8_t{1} : uint8_t{0};
                impactEvt.hadArmor = (armorBefore > 0.f) ? uint8_t{1} : uint8_t{0};
                impactEvt.damage = dealtDamage;
                impactEvt.target = hit.entity;
                impactEvt.pos1 = hit.point;
                impactEvt.pos2 = hit.normal;
                outParticles.push_back(impactEvt);
            }
        };

        if (gun.type == WeaponType::Shotgun) {
            // 11-pellet Peacekeeper pattern: 1 centre + inner pentagon (×0.5)
            // + outer pentagon (×1.0). The two rings share the same 5 angles
            // (72° spacing starting from straight-up), so each ray from the
            // centre carries two pellets at different radii. Outer ring sits
            // at the full ~5° spread; inner ring at ~2.5°. Each pellet runs
            // an independent raycast and emits its own tracer/impact, so the
            // HUD widget reads per-pellet hit/headshot from the replicated
            // NetParticleEvents.
            static constexpr int k_pelletCount = 11;
            static constexpr float k_spreadRad = 0.218f; // ~12.5° (outer ring) — 5× the original 0.0436 baseline
            // Pre-computed offsets in tangent plane. Order MUST match
            // ShotgunPelletWidget's k_pelletPositions so widget colours line
            // up with the actual ray that was fired.
            static constexpr std::array<std::pair<float, float>, k_pelletCount> k_offsets{{
                {0.0000f, 0.0000f}, // 0: centre
                // Inner pentagon (×0.5)
                {0.0000f, 0.5000f},   // 1: top
                {-0.4755f, 0.1545f},  // 2: upper-left
                {-0.2939f, -0.4045f}, // 3: lower-left
                {0.2939f, -0.4045f},  // 4: lower-right
                {0.4755f, 0.1545f},   // 5: upper-right
                // Outer pentagon (×1.0)
                {0.0000f, 1.0000f},   // 6: top
                {-0.9511f, 0.3090f},  // 7: upper-left
                {-0.5878f, -0.8090f}, // 8: lower-left
                {0.5878f, -0.8090f},  // 9: lower-right
                {0.9511f, 0.3090f},   // 10: upper-right
            }};
            const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
            // Avoid degenerate cross when looking nearly straight up/down.
            const glm::vec3 rightAxis = (std::abs(direction.y) > 0.99f)
                                            ? glm::vec3{1.0f, 0.0f, 0.0f}
                                            : glm::normalize(glm::cross(direction, worldUp));
            const glm::vec3 upAxis = glm::normalize(glm::cross(rightAxis, direction));
            const float tanSpread = std::tan(k_spreadRad);
            for (int i = 0; i < k_pelletCount; ++i) {
                const auto [ox, oy] = k_offsets[i];
                const glm::vec3 pelletDir = glm::normalize(direction + tanSpread * (ox * rightAxis + oy * upAxis));
                resolvePellet(pelletDir, /*logCenter=*/i == 0);
            }
        } else {
            resolvePellet(direction, /*logCenter=*/true);
        }

    } else {
        // Spawn projectile
        ProjectileConfig projConfig = getProjectileConfig(gun.type);
        const entt::entity projectile = registry.create();
        registry.emplace<Projectile>(
            projectile,
            Projectile{.type = gun.type, .damage = config.damage, .owner = shooter, .explosive = config.explosive});
        registry.emplace<Position>(projectile, Position{.value = muzzle});
        registry.emplace<Velocity>(projectile, Velocity{.value = direction * config.initialProjectileSpeed});
        registry.emplace<CollisionShape>(projectile, projConfig.shape);
    }
}

void runWeapon(Registry& registry,
               float dt,
               std::vector<NetParticleEvent>& outParticles,
               std::vector<NetKillEvent>& killEvents,
               std::vector<net::shotdebug::ShotDebugCapture>* outShotDebug)
{
    auto view = registry.view<InputSnapshot, Position, CollisionShape, WeaponState, GrenadeState, PlayerVisState>();
    view.each([&](entt::entity shooter,
                  InputSnapshot& input,
                  const Position& pos,
                  const CollisionShape& shape,
                  WeaponState& weapon,
                  GrenadeState& grenades,
                  const PlayerVisState& vis) {
        // Dead players cannot fire or interact with weapons.
        if (registry.all_of<RespawnTimer>(shooter))
            return;

        handleSwitch(input, weapon);
        handleCooldown(weapon, dt);
        handleGrenadeCooldown(grenades, dt);

        // Clear beam state when switching away from a beam weapon.
        const GunInstance& equipped = getEquippedGun(weapon);
        const WeaponConfig& cfg = getWeaponConfig(equipped.type);
        if (!cfg.isBeam) {
            if (auto* beam = registry.try_get<BeamState>(shooter))
                beam->active = false;
        }

        handleGrenadeInput(registry, shooter, input, pos, shape, grenades, vis.gravityFlipped);

        handleScope(registry, shooter, input, weapon, dt);

        // Firing is locked out during the throw wind-up (first kGrenadeThrowAnimTime
        // seconds of the throw cooldown), mirroring the client viewmodel dip.
        const float throwElapsed = getGrenadeConfig(grenades.selected).throwCooldown - grenades.cooldown;
        const bool grenadeThrowActive =
            grenades.cooldown > 0.0f && throwElapsed >= 0.0f && throwElapsed < kGrenadeThrowAnimTime;

        handleFire(registry,
                   shooter,
                   input,
                   pos,
                   shape,
                   weapon,
                   vis.gravityFlipped,
                   grenadeThrowActive,
                   dt,
                   outParticles,
                   killEvents,
                   outShotDebug);
        if (input.reload) {
            GunInstance& gun = getEquippedGun(weapon);
            handleReload(gun);
        }

        // Debug: refill all weapons when the client requests it.
        if (input.refillAmmo) {
            auto refill = [](GunInstance& g) {
                const WeaponConfig& c = getWeaponConfig(g.type);
                g.currentMagAmmo = c.magazineSize;
                g.totalAmmo = c.defaultAmmoCapacity;
            };
            refill(getSlot(weapon, WeaponSlot::PRIMARY));
            refill(getSlot(weapon, WeaponSlot::SECONDARY));
            for (std::size_t i = 0; i < kGrenadeTypes.size(); ++i) {
                grenades.ammo[i] = getWeaponConfig(kGrenadeTypes[i]).defaultAmmoCapacity;
            }
            input.refillAmmo = false; // consume the flag
        }

        // Debug: replace a slot's weapon when the client requests it. Resets
        // mag/ammo/reload/cooldown so the new weapon comes up clean. -1 means
        // "no change"; we consume the request (write back -1) so it pulses once.
        auto setSlotWeapon = [](GunInstance& g, WeaponType t) {
            const WeaponConfig& c = getWeaponConfig(t);
            g.type = t;
            g.currentMagAmmo = c.magazineSize;
            g.totalAmmo = c.defaultAmmoCapacity;
            g.fireCooldown = 0.f;
            g.chargeTime = 0.f;
            g.isReloading = false;
            g.reloadTime = 0.f;
            g.recoilHeat = 0.f;
            g.recoilIdleTime = 0.f;
        };
        if (input.debugSetPrimaryWeapon >= 0) {
            setSlotWeapon(getSlot(weapon, WeaponSlot::PRIMARY), static_cast<WeaponType>(input.debugSetPrimaryWeapon));
            input.debugSetPrimaryWeapon = -1;
        }
        if (input.debugSetSecondaryWeapon >= 0) {
            setSlotWeapon(getSlot(weapon, WeaponSlot::SECONDARY),
                          static_cast<WeaponType>(input.debugSetSecondaryWeapon));
            input.debugSetSecondaryWeapon = -1;
        }
    });
}

} // namespace systems
