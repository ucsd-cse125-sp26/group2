/// @file WeaponSystem.cpp
/// @brief Weapon state manager system.

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/registry/Registry.hpp"


namespace systems
{


inline GunInstance& getEquippedGun(WeaponState& weapon)
{
    return (weapon.current == WeaponSlot::PRIMARY) ? weapon.primary : weapon.secondary;
}

void handleSwitch(const InputSnapshot& input, WeaponState& weapon)
{
    if (input.switchToPrimary) {
        weapon.current = WeaponSlot::PRIMARY;
    } else if (input.switchToSecondary) {
        weapon.current = WeaponSlot::SECONDARY;
    }
}

inline void handleCooldown(WeaponState& weapon, float dt)
{
    auto reduce = [dt](GunInstance& gun) {
        gun.fireCooldown = std::max(0.0f, gun.fireCooldown - dt);
    };

    reduce(weapon.primary);
    reduce(weapon.secondary);
}

inline bool handleAmmo(GunInstance& gun)
{
    if (gun.currentMagAmmo <= 0) {
        return false;
    }

    --gun.currentMagAmmo;
    return true;
}

inline void handleFire(const InputSnapshot& input, WeaponState& weapon)
{
    if (!input.shooting) {
        return;
    }

    GunInstance& gun = getEquippedGun(weapon);

    if (gun.fireCooldown > 0.0f) {
        return;
    }

    if (!handleAmmo(gun)) {
        return;
    }

    // Set the cooldown timer
    const WeaponConfig& config = getWeaponConfig(gun.type);
    gun.fireCooldown = config.fireCooldown;

}

void runWeapon(Registry& registry, float dt)
{
    auto view = registry.view<InputSnapshot, WeaponState>();
    view.each([dt](const InputSnapshot& input, WeaponState& weapon) {
        handleSwitch(input, weapon);
        handleCooldown(weapon, dt);
        handleFire(input, weapon);
    });
}

}
