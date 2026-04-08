#pragma once

#include <glm/vec3.hpp>

// Pure physics math — no ECS types, no registry.
// All functions take values in and return values out.
// Constants come from PhysicsConstants.hpp.
namespace physics
{
// Apply gravity: subtract k_gravity * dt from the Y component.
// Call every tick when the entity is airborne (not grounded).
glm::vec3 applyGravity(glm::vec3 vel, float dt);

// Apply Quake-style ground friction to horizontal (XZ) velocity.
// Uses k_stopSpeed threshold so entities stop crisply rather than
// asymptotically approaching zero.
// Call every tick when the entity is grounded.
glm::vec3 applyGroundFriction(glm::vec3 vel, float dt);

// Quake PM_Accelerate: accelerate toward wishDir up to wishSpeed.
//
// Key property: this does NOT cap total speed — only the projection of
// velocity onto wishDir is capped at wishSpeed. Existing momentum in any
// other direction is untouched. This is what makes strafe jumping possible.
//
// wishDir   — normalised desired movement direction (from InputSnapshot + yaw)
// wishSpeed — target speed (k_maxGroundSpeed on ground, k_airMaxSpeed in air)
// accel     — acceleration constant (k_groundAccel or k_airAccel)
glm::vec3 accelerate(glm::vec3 vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt);

// Project velocity onto a collision surface to slide along it.
// overbounce: k_overbounceFloor for floors,
//             k_overbounceWall  for walls/ceilings.
glm::vec3 clipVelocity(glm::vec3 vel, glm::vec3 normal, float overbounce);
} // namespace physics
