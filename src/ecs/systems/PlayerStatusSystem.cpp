/// @file PlayerStatusSystem.cpp
/// @brief Player status manager system.

#include "PlayerStatusSystem.hpp"

#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/registry/Registry.hpp"

namespace systems
{
void applyHeal(float amount, Health& playerHealth)
{
    if (amount < 0)
        return;

    if (playerHealth.health < systems::healthMax) {
        if ((playerHealth.health + amount) > systems::healthMax) {
            amount -= systems::healthMax - playerHealth.health;
            playerHealth.health = systems::healthMax;
            playerHealth.armor = amount;
        } else {
            playerHealth.health += amount;
        }

    } else if ((playerHealth.armor + amount) <= systems::armorMax) {
        playerHealth.armor += amount;
    } else {
        playerHealth.armor = systems::armorMax;
    }
}

inline void handleRespawn(entt::entity& player, Registry& registry)
{
    // Refresh player stats and position
    const WeaponConfig& rifleConfig = getWeaponConfig(WeaponType::Rifle);
    const WeaponConfig& railConfig = getWeaponConfig(WeaponType::RailGun);
    const WeaponConfig& wingmanConfig = getWeaponConfig(WeaponType::EnergyGun);
    const WeaponConfig& rocketConfig = getWeaponConfig(WeaponType::Rocket);

    registry.emplace_or_replace<InputSnapshot>(player);
    registry.emplace_or_replace<Position>(player, glm::vec3{0.0f, 200.0f, 0.0f});
    registry.emplace_or_replace<Velocity>(player);
    registry.emplace_or_replace<PlayerState>(player);
    registry.emplace_or_replace<Health>(player, Health{});
    registry.emplace_or_replace<WeaponState>(player,
                                             WeaponState{
                                                 .primary =
                                                     GunInstance{
                                                         .type = WeaponType::Rifle,
                                                         .totalAmmo = rifleConfig.defaultAmmoCapacity,
                                                         .currentMagAmmo = rifleConfig.magazineSize,
                                                         .fireCooldown = 0.0f,
                                                     },
                                                 .secondary =
                                                     GunInstance{
                                                         .type = WeaponType::RailGun,
                                                         .totalAmmo = railConfig.defaultAmmoCapacity,
                                                         .currentMagAmmo = railConfig.magazineSize,
                                                         .fireCooldown = 0.0f,
                                                     },
                                                 .tertiary =
                                                     GunInstance{
                                                         .type = WeaponType::EnergyGun,
                                                         .totalAmmo = wingmanConfig.defaultAmmoCapacity,
                                                         .currentMagAmmo = wingmanConfig.magazineSize,
                                                         .fireCooldown = 0.0f,
                                                     },
                                                 .quaternary =
                                                     GunInstance{
                                                         .type = WeaponType::Rocket,
                                                         .totalAmmo = rocketConfig.defaultAmmoCapacity,
                                                         .currentMagAmmo = rocketConfig.magazineSize,
                                                         .fireCooldown = 0.0f,
                                                     },
                                                 .current = WeaponSlot::PRIMARY,
                                             });
}

inline void handleDeath(entt::entity& player, Health& playerHealth, entt::entity& killer, Registry& registry)
{
    if (playerHealth.health <= 0) {
        // Update death
        registry.get_or_emplace<PlayerState>(player).IsDead = true;
        registry.patch<PlayerMatchStats>(player, [&](PlayerMatchStats& stats) { stats.deaths++; });

        // Award killer
        registry.patch<PlayerMatchStats>(killer, [&](PlayerMatchStats& stats) { stats.kills++; });

        // Respawn
        handleRespawn(player, registry);
    }
}

void applyDamage(float damage, entt::entity player, entt::entity& killer, Registry& registry)
{
    Health& playerHealth = registry.get_or_emplace<Health>(player);

    // Reset heal cooldown on every damage tick
    playerHealth.healTimer = systems::healCooldown;

    if (playerHealth.armor >= damage) {
        playerHealth.armor -= damage;
    } else {
        const float overflow = damage - playerHealth.armor;
        playerHealth.armor = 0;
        if (playerHealth.health - overflow <= 0) {
            playerHealth.health = 0;
            handleDeath(player, playerHealth, killer, registry);
        } else {
            playerHealth.health -= overflow;
        }
    }
}

inline void handleHealing(Health& playerHealth, float dt)
{
    if (playerHealth.healTimer == 0) {
        const float healingAmount = healingRate * dt;
        applyHeal(healingAmount, playerHealth);
    } else {
        playerHealth.healTimer -= dt;
        if (playerHealth.healTimer < 0)
            playerHealth.healTimer = 0;
    }
}

void runPlayerStatus(Registry& registry, float dt)
{
    registry.view<Player>().each([&registry, dt](entt::entity e) {
        Health& playerHealth = registry.get_or_emplace<Health>(e);
        handleHealing(playerHealth, dt);
    });
}
} // namespace systems
