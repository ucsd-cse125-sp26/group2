/// @file WeaponSystem.cpp
/// @brief Weapon state manager system.

#include "ecs/systems/WeaponSystem.hpp"

#include "PlayerStatusSystem.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/registry/Registry.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

using physics::HitscanHit;
using physics::resolveHitscan;

namespace systems
{

inline GunInstance& getEquippedGun(WeaponState& weapon)
{
    switch (weapon.current) {
    case WeaponSlot::SECONDARY:
        return weapon.secondary;
    case WeaponSlot::TERTIARY:
        return weapon.tertiary;
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
    }
}

inline void handleCooldown(WeaponState& weapon, float dt)
{
    auto reduce = [dt](GunInstance& gun) { gun.fireCooldown = std::max(0.0f, gun.fireCooldown - dt); };

    reduce(weapon.primary);
    reduce(weapon.secondary);
    reduce(weapon.tertiary);
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

inline void handleFire(Registry& registry,
                       entt::entity shooter,
                       const InputSnapshot& input,
                       const Position& pos,
                       const CollisionShape& shape,
                       WeaponState& weapon,
                       float dt,
                       std::vector<NetParticleEvent>& outParticles)
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
        const HitscanHit hit = resolveHitscan(registry, shooter, eye, direction);

        // Apply DPS-based damage.
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            applyDamage(config.dps * dt, hit.entity, shooter, registry);
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
        const HitscanHit hit = resolveHitscan(registry, shooter, eye, direction);

        // Charge damage — significantly higher than normal.
        if (hit.entity != entt::null && registry.valid(hit.entity)) {
            applyDamage(config.chargeDamage, hit.entity, shooter, registry);
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
            impactEvt.surfaceType = hit.surface;
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

    if (!config.hitscan) {
        return;
    }

    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f, 0.0f};
    const glm::vec3 direction = viewForward(input.yaw, input.pitch);
    const HitscanHit hit = resolveHitscan(registry, shooter, eye, direction);

    // Apply damage
    if (hit.entity != entt::null && registry.valid(hit.entity)) {
        applyDamage(config.damage, hit.entity, shooter, registry);
    }

    // Emit replicated particle events for client FX.
    // 1) Tracer or beam from muzzle to hit point.
    {
        NetParticleEvent tracerEvt;
        tracerEvt.source = shooter;
        tracerEvt.weaponType = gun.type;
        if (gun.type == WeaponType::RailGun) {
            tracerEvt.effectType = ParticleEffectType::HitscanBeam;
            tracerEvt.pos1 = eye;
            tracerEvt.pos2 = hit.point;
        } else {
            tracerEvt.effectType = ParticleEffectType::BulletTracer;
            tracerEvt.pos1 = eye;
            tracerEvt.pos2 = glm::normalize(hit.point - eye);
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
        impactEvt.surfaceType = hit.surface;
        impactEvt.pos1 = hit.point;
        impactEvt.pos2 = hit.normal;
        outParticles.push_back(impactEvt);
    }
}

void runWeapon(Registry& registry, float dt, std::vector<NetParticleEvent>& outParticles)
{
    registry.view<InputSnapshot, Position, CollisionShape, WeaponState>().each([&](entt::entity shooter,
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

        handleFire(registry, shooter, input, pos, shape, weapon, dt, outParticles);
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
