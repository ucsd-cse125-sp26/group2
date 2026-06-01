/// @file PickupGeometry.hpp
/// @brief Shared geometry helpers for weapon-pickup proximity tests.
///
/// Used by WeaponSpawnerSystem, DroppedWeaponSystem, and the client-side
/// HUD pickup-prompt sweep so the "is the player close enough and looking
/// at the pickup" rule lives in one place.

#pragma once

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace systems
{

/// @brief Maximum distance (units) at which a player can press F to pick up.
constexpr float k_pickupRange = 140.0f;

/// @brief Shared pickup collision half-extents for weapon spawners and dropped weapons.
inline const glm::vec3 k_weaponPickupHalfExtents{32.0f, 32.0f, 32.0f};

/// @brief Half-angle (degrees) of the look cone for press-F pickup.
constexpr float k_pickupMaxAngleDeg = 12.0f;

/// @brief Pre-computed cosine of the look cone half-angle.
inline const float k_pickupMinDot = std::cos(glm::radians(k_pickupMaxAngleDeg));

/// @brief Test whether two axis-aligned bounding boxes overlap.
inline bool overlapsAABB(glm::vec3 aPos, glm::vec3 aHalf, glm::vec3 bPos, glm::vec3 bHalf)
{
    return std::abs(aPos.x - bPos.x) <= (aHalf.x + bHalf.x) && std::abs(aPos.y - bPos.y) <= (aHalf.y + bHalf.y) &&
           std::abs(aPos.z - bPos.z) <= (aHalf.z + bHalf.z);
}

/// @brief Forward direction implied by a player's yaw + pitch.
inline glm::vec3 viewForward(float yaw, float pitch)
{
    const float cp = std::cos(pitch);
    return glm::normalize(glm::vec3{
        std::sin(yaw) * cp,
        -std::sin(pitch),
        std::cos(yaw) * cp,
    });
}

/// @brief True if the eye is within range of `pickupPos` and `viewFwd` points within the look cone.
inline bool isPlayerLookingAtPickup(glm::vec3 eye, glm::vec3 viewFwd, glm::vec3 pickupPos)
{
    const glm::vec3 toPickup = pickupPos - eye;
    const float distSq = glm::dot(toPickup, toPickup);
    if (distSq > k_pickupRange * k_pickupRange || distSq <= 0.0001f)
        return false;
    return glm::dot(viewFwd, glm::normalize(toPickup)) >= k_pickupMinDot;
}

} // namespace systems
