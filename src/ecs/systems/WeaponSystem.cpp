/// @file WeaponSystem.cpp
/// @brief Weapon state manager system.

#include "ecs/systems/WeaponSystem.hpp"

#include "PlayerStatusSystem.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/HitboxHistory.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LagCompTarget.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/WorldData.hpp"
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
#include <glm/geometric.hpp>

using physics::HitboxHit;
using physics::HitscanHit;
using physics::resolveHitscan;
using physics::resolveHitscanHitbox;

namespace systems
{

/// @brief Return the GunInstance for the currently selected weapon slot.
/// @param weapon  Weapon state to query.
/// @return Reference to the equipped gun.
inline GunInstance& getEquippedGun(WeaponState& weapon)
{
    if (weapon.current == WeaponSlot::PRIMARY) {
        return weapon.primary;
    }
    return weapon.secondary;
}

/// @brief Apply weapon slot switch from player input.
/// @param input   Current input snapshot.
/// @param weapon  Weapon state (modified in place).
void handleSwitch(const InputSnapshot& input, WeaponState& weapon)
{
    if (input.switchToPrimary) {
        weapon.current = WeaponSlot::PRIMARY;
    } else if (input.switchToSecondary) {
        weapon.current = WeaponSlot::SECONDARY;
    }
}

/// @brief Tick fire cooldowns for all weapon slots.
/// @param weapon  Weapon state (modified in place).
/// @param dt      Fixed physics delta time in seconds.
inline void handleCooldown(WeaponState& weapon, float dt)
{
    auto reduce = [dt](GunInstance& gun) { gun.fireCooldown = std::max(0.0f, gun.fireCooldown - dt); };

    reduce(weapon.primary);
    reduce(weapon.secondary);
}

/// @brief Reload the gun's magazine from reserve ammo.
/// @param gun  Gun instance to reload (modified in place).
inline void handleReload(GunInstance& gun)
{
    const WeaponConfig& config = getWeaponConfig(gun.type);
    if (gun.totalAmmo > 0 && gun.currentMagAmmo < config.magazineSize) {
        int reloadAmount = config.magazineSize - gun.currentMagAmmo;
        if ((gun.totalAmmo - reloadAmount) >= 0) {
            gun.currentMagAmmo += reloadAmount;
            gun.totalAmmo -= reloadAmount;
        } else {
            gun.currentMagAmmo += gun.totalAmmo;
            gun.totalAmmo = 0;
        }
    }
}

/// @brief Consume one round from the magazine; auto-reload if empty.
/// @param gun  Gun instance to consume ammo from (modified in place).
/// @return True if a round was consumed, false if the gun is empty.
inline bool handleAmmo(GunInstance& gun)
{
    if (gun.currentMagAmmo <= 0) {
        // TODO: Need to implement reload state so it is not instant.
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
/// @param eye        Eye position (camera origin).
/// @param direction  Normalized view direction.
/// @return Offset muzzle position in world space.
inline glm::vec3 muzzleOrigin(glm::vec3 eye, glm::vec3 direction)
{
    constexpr glm::vec3 k_worldUp{0.0f, 1.0f, 0.0f};
    glm::vec3 right = glm::cross(direction, k_worldUp);
    if (glm::dot(right, right) < physics::k_parallelEpsilon) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        right = glm::normalize(right);
    }

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
/// @param registry      The ECS registry.
/// @param shooter       Entity that is firing.
/// @param input         Current input snapshot.
/// @param pos           Shooter position.
/// @param shape         Shooter collision shape (for eye height).
/// @param weapon        Shooter weapon state (modified in place).
/// @param dt            Fixed physics delta time in seconds.
/// @param outParticles  Accumulates particle events for network broadcast.
/// @param killEvents    Accumulates kill events for network broadcast.
/// @param outShotDebug  PR-20: optional server-side debug capture sink.
///                      When non-null, each hitscan-fire path pushes one
///                      `ShotDebugCapture` row while `RewindHitboxesGuard`
///                      is still active, so the captured `targets[*].
///                      capsules` reflect the historical sample the
///                      server actually raycast against.
inline void handleFire(Registry& registry,
                       entt::entity shooter,
                       const InputSnapshot& input,
                       const Position& pos,
                       const CollisionShape& shape,
                       WeaponState& weapon,
                       float dt,
                       std::vector<NetParticleEvent>& outParticles,
                       std::vector<NetKillEvent>& killEvents,
                       std::vector<net::shotdebug::ShotDebugCapture>* outShotDebug)
{
    GunInstance& gun = getEquippedGun(weapon);
    const WeaponConfig& config = getWeaponConfig(gun.type);

    // ── Beam weapon path ──
    if (config.isBeam) {
        auto& beam = registry.get_or_emplace<BeamState>(shooter);

        if (!input.shooting || gun.currentMagAmmo <= 0) {
            beam.active = false;
            return;
        }

        // Drain ammo over time (fractional accumulation).
        gun.fireCooldown += config.ammoPerSecond * dt; // repurpose cooldown as drain accumulator
        if (gun.fireCooldown >= 1.0f) {
            const int drain = static_cast<int>(gun.fireCooldown);
            gun.currentMagAmmo = std::max(0, gun.currentMagAmmo - drain);
            gun.fireCooldown -= static_cast<float>(drain);
        }

        // Raycast to find beam endpoint.
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f, 0.0f};
        const glm::vec3 direction = viewForward(input.yaw, input.pitch);
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
        const auto rewindGuard = systems::rewindHitboxes(registry, shooter, &eye, &direction, physics::k_hitscanRange);
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction);

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
        }

        // Update BeamState (synced to clients via registry snapshot).
        beam.active = true;
        beam.type = gun.type;
        beam.origin = eye;
        beam.hitPoint = hit.point;
        return;
    }

    // ── Charge weapon path ──
    // Hold fire to charge, release to fire.  chargeTime accumulates while
    // holding; the shot fires on the first tick where shooting becomes false.
    if (config.isCharge) {
        if (input.shooting) {
            // Charging — accumulate time, do not fire yet.
            gun.chargeTime += dt;
            return;
        }
        if (gun.chargeTime <= 0.0f) {
            // Not charging, not shooting — nothing to do.
            return;
        }

        // Button released with charge built up → fire the shot.
        if (gun.fireCooldown > 0.0f) {
            gun.chargeTime = 0.0f;
            return;
        }
        if (!handleAmmo(gun)) {
            gun.chargeTime = 0.0f;
            return;
        }

        gun.fireCooldown = config.fireCooldown;

        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f, 0.0f};
        const glm::vec3 direction = viewForward(input.yaw, input.pitch);
        // Phase 6 lag-compensated hitscan (see beam path for details).
        // PR-5: ray-filtered rewind, see beam path.
        const auto rewindGuard = systems::rewindHitboxes(registry, shooter, &eye, &direction, physics::k_hitscanRange);
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction);

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

        // Charge damage with body-region multiplier.
        float chargeDealtDamage = 0.f;
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            const float multiplier = defaultDamageProfile().multipliers[static_cast<size_t>(hit.region)];
            chargeDealtDamage = config.chargeDamage * multiplier;
            applyDamage(chargeDealtDamage, hit.entity, shooter, registry, killEvents, hit.region);
            if (hit.region == BodyRegion::Head) {
                SDL_Log("[weapon] HEADSHOT! charge weapon hit %d in head for %.0f damage",
                        static_cast<int>(hit.entity),
                        static_cast<double>(chargeDealtDamage));
            }
        }

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

        gun.chargeTime = 0.0f;
        return;
    }

    // ── Discrete weapon path ──
    if (!input.shooting) {
        return;
    }

    if (gun.fireCooldown > 0.0f) {
        return;
    }

    if (!handleAmmo(gun)) {
        return;
    }

    gun.fireCooldown = config.fireCooldown;

    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f, 0.0f};
    const glm::vec3 direction = viewForward(input.yaw, input.pitch);
    const glm::vec3 muzzle = muzzleOrigin(eye, direction);

    if (config.hitscan) {
        // Phase 6 lag-compensated hitscan (see beam path for details).
        // PR-5: ray-filtered rewind.
        const auto rewindGuard = systems::rewindHitboxes(registry, shooter, &eye, &direction, physics::k_hitscanRange);
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction);

        // PR-18b: log to server-side shot-resolution CSV.  Discrete
        // hitscan path: one log row per click-fire.
        logShot(registry, shooter, input.tick, eye, direction, hit);
        // PR-20: capture rewound state for the live debug visualizer.
        captureShotDebug(registry, shooter, input.tick, eye, direction, physics::k_hitscanRange, hit, outShotDebug);

        // Snapshot armor before damage for shield-break detection.
        float armorBefore = 0.f;
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            if (const auto* hp = registry.try_get<Health>(hit.entity))
                armorBefore = hp->armor;
        }

        // Apply damage with body-region multiplier.
        float dealtDamage = 0.f;
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            const float multiplier = defaultDamageProfile().multipliers[static_cast<size_t>(hit.region)];
            dealtDamage = config.damage * multiplier;
            applyDamage(dealtDamage, hit.entity, shooter, registry, killEvents, hit.region);
            if (hit.region == BodyRegion::Head) {
                SDL_Log("[weapon] HEADSHOT! %d hit %d for %.0f damage (base %.0f x %.1f)",
                        static_cast<int>(shooter),
                        static_cast<int>(hit.entity),
                        static_cast<double>(dealtDamage),
                        static_cast<double>(config.damage),
                        static_cast<double>(multiplier));
            }
        }

        // Check if armor was just depleted to zero by this shot.
        bool shieldBroke = false;
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            if (const auto* hp = registry.try_get<Health>(hit.entity))
                shieldBroke = (armorBefore > 0.f && hp->armor <= 0.f);
        }

        // Emit replicated particle events for client FX.
        // 1) Tracer or beam from muzzle to hit point.
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
    auto view = registry.view<InputSnapshot, Position, CollisionShape, WeaponState>();
    view.each([&](entt::entity shooter,
                  InputSnapshot& input,
                  const Position& pos,
                  const CollisionShape& shape,
                  WeaponState& weapon) {
        handleSwitch(input, weapon);
        handleCooldown(weapon, dt);

        // Clear beam state when switching away from a beam weapon.
        const GunInstance& equipped = getEquippedGun(weapon);
        const WeaponConfig& cfg = getWeaponConfig(equipped.type);
        if (!cfg.isBeam) {
            if (auto* beam = registry.try_get<BeamState>(shooter))
                beam->active = false;
        }

        handleFire(registry, shooter, input, pos, shape, weapon, dt, outParticles, killEvents, outShotDebug);
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
            refill(weapon.primary);
            refill(weapon.secondary);
            input.refillAmmo = false; // consume the flag
        }
    });
}

} // namespace systems
