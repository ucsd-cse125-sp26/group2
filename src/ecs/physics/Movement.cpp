#include "Movement.hpp"

#include "PhysicsConstants.hpp"

#include <algorithm>
#include <glm/geometric.hpp>

namespace physics
{

glm::vec3 applyGravity(glm::vec3 vel, float dt)
{
    vel.y -= k_gravity * dt;
    return vel;
}

glm::vec3 applyGroundFriction(glm::vec3 vel, float dt)
{
    // Only horizontal (XZ) velocity is affected by ground friction.
    const float k_speed = glm::length(glm::vec3(vel.x, 0.0f, vel.z));
    if (k_speed < 0.001f)
        return vel;

    // Quake trick: use k_stopSpeed as a minimum so friction is amplified at
    // low speeds, giving a crisp stop rather than an asymptotic glide.
    const float k_control = std::max(k_speed, k_stopSpeed);
    const float k_drop = k_control * k_friction * dt;
    const float k_newSpeed = std::max(0.0f, k_speed - k_drop) / k_speed;

    return {vel.x * k_newSpeed, vel.y, vel.z * k_newSpeed};
}

glm::vec3 accelerate(glm::vec3 vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt)
{
    // Project current velocity onto the wish direction.
    // If we're already moving at or above wishSpeed in that direction, do nothing.
    const float k_currentSpeed = glm::dot(vel, wishDir);
    const float k_addSpeed = wishSpeed - k_currentSpeed;
    if (k_addSpeed <= 0.0f)
        return vel;

    // Accelerate, but never overshoot wishSpeed in the wish direction.
    const float k_accelSpeed = std::min(accel * dt * wishSpeed, k_addSpeed);
    return vel + wishDir * k_accelSpeed;
}

glm::vec3 clipVelocity(glm::vec3 vel, glm::vec3 normal, float overbounce)
{
    const float k_backoff = glm::dot(vel, normal) * overbounce;
    return vel - normal * k_backoff; // anti wall sticking
}

} // namespace physics
