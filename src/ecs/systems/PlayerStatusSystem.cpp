/// @file PlayerStatusSystem.cpp
/// @brief Player status manager system.

#include "PlayerStatusSystem.hpp"

#include "SDL3/SDL_log.h"
#include "ecs/components/DeathInfo.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerSimState.hpp" // also pulls in PlayerVisState
#include "ecs/components/Position.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/NetKillEvent.hpp"

#include <vector>

namespace systems
{
/// @copydoc applyHeal
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

/// @brief Reset a dead player to a fresh spawn state.
///
/// Clears the respawn timer and death info, restores visibility, resets
/// position/velocity/health/weapons to defaults, and places the player
/// at the spawn point.
///
/// @param player    Entity to respawn (modified in place).
/// @param registry  The ECS registry.
inline void handleRespawn(entt::entity& player, Registry& registry)
{
    const WeaponConfig& rifleConfig = getWeaponConfig(WeaponType::Rifle);
    const WeaponConfig& railConfig = getWeaponConfig(WeaponType::RailGun);
    const WeaponConfig& wingmanConfig = getWeaponConfig(WeaponType::EnergyGun);
    const WeaponConfig& rocketConfig = getWeaponConfig(WeaponType::Rocket);

    registry.erase<RespawnTimer>(player);
    registry.erase<DeathInfo>(player);
    registry.patch<Renderable>(player, [](Renderable& rend) { rend.visible = true; });
    registry.emplace_or_replace<InputSnapshot>(player);
    registry.emplace_or_replace<Position>(player, glm::vec3{0.0f, 200.0f, 0.0f});
    registry.emplace_or_replace<Velocity>(player);
    registry.emplace_or_replace<PlayerVisState>(player);
    registry.emplace_or_replace<PlayerSimState>(player);
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

/// @brief Transition a player to the dead state if health has reached zero.
///
/// Hides the player, removes hitboxes, starts a 5-second respawn timer,
/// updates death/kill stats, and emits a NetKillEvent for the kill feed.
///
/// @param player       Entity that died.
/// @param playerHealth Health component (already at or below zero).
/// @param killer       Entity that dealt the killing blow.
/// @param registry     The ECS registry.
/// @param killEvents   Accumulates kill events for network broadcast.
/// @param hitRegion    Body region of the killing blow.
inline void handleDeath(entt::entity& player,
                        Health& playerHealth,
                        entt::entity& killer,
                        Registry& registry,
                        std::vector<NetKillEvent>& killEvents,
                        BodyRegion hitRegion)
{
    if (playerHealth.health <= 0) {
        // Update death
        registry.get_or_emplace<PlayerVisState>(player).isDead = true;
        registry.get_or_emplace<Velocity>(player) = Velocity{};
        registry.patch<Renderable>(player, [](Renderable& rend) { rend.visible = false; });
        registry.remove<HitboxInstance>(player);
        registry.emplace_or_replace<RespawnTimer>(player, RespawnTimer{.timeRemaining = 5.0f});
        registry.patch<PlayerMatchStats>(player, [&](PlayerMatchStats& stats) { stats.deaths++; });

        // Award killer
        if (killer != player) {
            registry.get_or_emplace<PlayerMatchStats>(killer).kills++;
        }

        // Get killer info
        ClientId killerId = registry.get<ClientId>(killer);
        Health killerHealth = registry.get<Health>(killer);

        // Death info handling
        NetKillEvent event{
            .killerId = killerId,
            .victimId = registry.get<ClientId>(player),
            .killerHealth = killerHealth,
            .hitRegion = hitRegion,
            .isHeadshot = (hitRegion == BodyRegion::Head),
        };
        killEvents.push_back(event);

        registry.emplace_or_replace<DeathInfo>(player,
                                               DeathInfo{
                                                   .killerId = killerId,
                                                   .killerHealth = killerHealth,
                                               });
    }
}

void applyDamage(float damage,
                 entt::entity player,
                 entt::entity& killer,
                 Registry& registry,
                 std::vector<NetKillEvent>& killEvents,
                 BodyRegion hitRegion)
{
    // If player is dead, ignore damage
    if (registry.all_of<RespawnTimer>(player))
        return;

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
            handleDeath(player, playerHealth, killer, registry, killEvents, hitRegion);
        } else {
            playerHealth.health -= overflow;
        }
    }
}

/// @brief Tick passive health regeneration after the heal cooldown expires.
/// @param playerHealth  Health component (modified in place).
/// @param dt            Fixed physics delta time in seconds.
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
        if (registry.all_of<RespawnTimer>(e)) {
            auto& respawnTimer = registry.get<RespawnTimer>(e);
            respawnTimer.timeRemaining -= dt;
            if (respawnTimer.timeRemaining <= 0) {
                handleRespawn(e, registry);
            }
        } else {
            Health& playerHealth = registry.get_or_emplace<Health>(e);
            handleHealing(playerHealth, dt);
        }
    });
}
} // namespace systems
