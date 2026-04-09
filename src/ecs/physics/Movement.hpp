#pragma once

#include <glm/vec3.hpp>

/// @brief Pure physics math — no ECS types, no registry.
///
/// All functions take values in and return values out (no mutation via pointer).
/// Constants come from PhysicsConstants.hpp.
namespace physics
{

/// @brief Apply gravity for one tick: subtracts `k_gravity * dt` from the Y component.
/// @param vel  Current velocity.
/// @param dt   Delta time in seconds.
/// @return     New velocity with gravity applied.
/// @note       Call every tick when the entity is airborne (not grounded).
glm::vec3 applyGravity(glm::vec3 vel, float dt);

/// @brief Apply Quake-style ground friction to horizontal (XZ) velocity.
///
/// Uses `k_stopSpeed` as a minimum control speed so entities stop crisply
/// rather than asymptotically approaching zero.
///
/// @param vel  Current velocity.
/// @param dt   Delta time in seconds.
/// @return     New velocity with friction applied to XZ; Y is unchanged.
/// @note       Call every tick when the entity is grounded.
glm::vec3 applyGroundFriction(glm::vec3 vel, float dt);

/// @brief Quake PM_Accelerate: accelerate toward `wishDir` up to `wishSpeed`.
///
/// Does **not** cap total speed — only the projection of velocity onto `wishDir`
/// is capped at `wishSpeed`. Existing momentum in any other direction is untouched.
/// This property is what makes strafe jumping possible.
///
/// @param vel        Current velocity.
/// @param wishDir    Normalised desired movement direction (from InputSnapshot + yaw).
/// @param wishSpeed  Target speed (k_maxGroundSpeed on ground, k_airMaxSpeed in air).
/// @param accel      Acceleration constant (k_groundAccel or k_airAccel).
/// @param dt         Delta time in seconds.
/// @return           New velocity with acceleration applied.
glm::vec3 accelerate(glm::vec3 vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt);

/// @brief Project velocity onto a collision surface to slide along it.
/// @param vel         Current velocity.
/// @param normal      Surface normal at the contact point.
/// @param overbounce  Separation scalar: use k_overbounceFloor for floors, k_overbounceWall for walls/ceilings.
/// @return            Clipped velocity that slides along the surface.
glm::vec3 clipVelocity(glm::vec3 vel, glm::vec3 normal, float overbounce);

/// @brief Compute the horizontal wish direction from yaw angle and WASD key state.
/// @param yaw      Player's current yaw in radians.
/// @param forward  True when W is held.
/// @param back     True when S is held.
/// @param left     True when A is held.
/// @param right    True when D is held.
/// @return         Normalised XZ direction vector, or `(0,0,0)` if no keys are pressed.
/// @note           Y component is always 0 — vertical movement is handled separately.
glm::vec3 computeWishDir(float yaw, bool forward, bool back, bool left, bool right);

} // namespace physics
