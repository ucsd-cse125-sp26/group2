/// @file HitboxSystem.hpp
/// @brief System that transforms skeleton-driven hitbox definitions from bone-local
///        space into world-space capsules every tick.
///
/// Reads:  Position, InputSnapshot (yaw), CollisionShape (halfExtents), JointMatrices
/// Writes: HitboxInstance

#pragma once

#include "ecs/components/Hitbox.hpp"
#include "ecs/registry/Registry.hpp"

namespace systems
{

/// @brief Update world-space hitbox capsules for all entities that have JointMatrices.
///
/// For each entity with [Position, JointMatrices] this builds the world transform
/// (entity position + yaw rotation + rig scale + vertical offset) and transforms
/// every HitboxDef capsule into a WorldCapsule stored in HitboxInstance.
///
/// @param registry    The ECS registry.
/// @param hitboxRig   Shared hitbox definitions (bone-to-capsule mapping).
/// @param rigScale    Scale factor applied to the rig (model space -> game units).
/// @param rigMeshMinY Minimum Y vertex of the bind-pose mesh (model space).
void updateHitboxes(Registry& registry, const HitboxRig& hitboxRig, float rigScale, float rigMeshMinY);

} // namespace systems
