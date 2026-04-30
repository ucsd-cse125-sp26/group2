/// @file WeaponSystem.cpp
/// @brief Weapon state manager system.

#include "ecs/systems/WeaponSystem.hpp"

#include "PlayerStatusSystem.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/registry/Registry.hpp"

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

inline GunInstance& getEquippedGun(WeaponState& weapon)
{
    switch (weapon.current) {
    case WeaponSlot::SECONDARY:
        return weapon.secondary;
    case WeaponSlot::TERTIARY:
        return weapon.tertiary;
    case WeaponSlot::QUATERNARY:
        return weapon.quaternary;
    default:
        return weapon.primary;
    }
}

void handleSwitch(const InputSnapshot& input, WeaponState& weapon)
{
    if (input.switchToPrimary) {
        weapon.current = WeaponSlot::PRIMARY;
    } else if (input.switchToSecondary) {
        weapon.current = WeaponSlot::SECONDARY;
    } else if (input.switchToTertiary) {
        weapon.current = WeaponSlot::TERTIARY;
    } else if (input.switchToQuaternary) {
        weapon.current = WeaponSlot::QUATERNARY;
    }
}

inline void handleCooldown(WeaponState& weapon, float dt)
{
    auto reduce = [dt](GunInstance& gun) { gun.fireCooldown = std::max(0.0f, gun.fireCooldown - dt); };

    reduce(weapon.primary);
    reduce(weapon.secondary);
    reduce(weapon.tertiary);
    reduce(weapon.quaternary);
}

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

inline void handleFire(Registry& registry,
                       entt::entity shooter,
                       const InputSnapshot& input,
                       const Position& pos,
                       const CollisionShape& shape,
                       WeaponState& weapon,
                       float dt,
                       std::vector<NetParticleEvent>& outParticles,
                       std::vector<NetKillEvent>& killEvents)
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
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction);

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
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction);

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
        const HitboxHit hit = resolveHitscanHitbox(registry, shooter, eye, direction);

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
               std::vector<NetKillEvent>& killEvents)
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

        handleFire(registry, shooter, input, pos, shape, weapon, dt, outParticles, killEvents);
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
            refill(weapon.tertiary);
            input.refillAmmo = false; // consume the flag
        }
    });
}

} // namespace systems
