#include "particles/effects/ExplosionVfxEffect.hpp"

#include <cassert>
#include <type_traits>

int main()
{
    assert(explosionVfxKindForWeapon(WeaponType::Rocket) == ExplosionVfxKind::Rocket);
    assert(explosionVfxKindForWeapon(WeaponType::HEGrenade) == ExplosionVfxKind::Frag);
    assert(explosionVfxKindForWeapon(WeaponType::Sticky) == ExplosionVfxKind::Sticky);
    assert(explosionVfxKindForWeapon(WeaponType::Molotov) == ExplosionVfxKind::Molotov);
    static_assert(sizeof(VfxSpriteParticle) == 96);
    static_assert(sizeof(VfxDebrisParticle) == 64);
    return 0;
}
