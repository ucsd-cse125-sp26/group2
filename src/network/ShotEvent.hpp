/// @file ShotEvent.hpp
/// @brief Wire-format particle effect event broadcast from server to all clients.

#pragma once

#include "ecs/components/Projectile.hpp"

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

/// @brief Type of particle effect to spawn on the client.
enum class ParticleEffectType : uint8_t
{
    BulletTracer, ///< pos1=origin, pos2=direction, param=distance
    HitscanBeam,  ///< pos1=origin, pos2=hitPos, weaponType
    Impact,       ///< pos1=position, pos2=normal, surfaceType, weaponType
    Explosion,    ///< pos1=position, param=blastRadius
    Smoke,        ///< pos1=position, param=radius
};

/// @brief Wire-format particle event broadcast from server to all clients.
///
/// General-purpose: any visual effect the server wants replicated goes
/// through this struct.  Fields are interpreted based on `effectType`.
struct NetParticleEvent
{
    entt::entity source = entt::null; ///< Entity that caused this (for skip-self logic).
    ParticleEffectType effectType = ParticleEffectType::BulletTracer;
    WeaponType weaponType = WeaponType::Rifle;
    SurfaceType surfaceType = SurfaceType::Concrete;
    uint8_t headshot = 0; ///< 1 if the impact hit the head region (for client hitmarker colour).
    glm::vec3 pos1{};     ///< Origin / position.
    glm::vec3 pos2{};     ///< Direction / hitPos / normal.
    float param = 0;      ///< Distance / radius.
};
static_assert(std::is_trivially_copyable_v<NetParticleEvent>);
