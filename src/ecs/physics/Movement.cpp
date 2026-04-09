#include "Movement.hpp"

#include "PhysicsConstants.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

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
    return vel - normal * k_backoff;
}

glm::vec3 computeWishDir(float yaw, bool forward, bool back, bool left, bool right)
{
    // Build a local XZ move vector from key state.
    // +Z is the forward direction in world space at yaw=0.
    float moveX = 0.0f;
    float moveZ = 0.0f;
    if (forward)
        moveZ += 1.0f;
    if (back)
        moveZ -= 1.0f;
    if (left)
        moveX -= 1.0f;
    if (right)
        moveX += 1.0f;

    if (moveX == 0.0f && moveZ == 0.0f)
        return glm::vec3{0.0f};

    // Rotate the local move vector by the player's yaw to get world-space direction.
    const float k_cosYaw = std::cos(yaw);
    const float k_sinYaw = std::sin(yaw);

    const glm::vec3 k_wish{moveX * k_cosYaw + moveZ * k_sinYaw, 0.0f, -moveX * k_sinYaw + moveZ * k_cosYaw};

    return glm::normalize(k_wish);
}

} // namespace physics
