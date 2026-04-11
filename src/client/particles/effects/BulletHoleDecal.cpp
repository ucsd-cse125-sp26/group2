/// @file BulletHoleDecal.cpp
/// @brief Implementation of ring-buffer decal pool for bullet holes and scorch marks.

#include "BulletHoleDecal.hpp"

#include <cmath>

void BulletHoleDecal::spawn(glm::vec3 pos, glm::vec3 normal, WeaponType wt)
{
    const uint32_t idx = head_ % k_max;
    ++head_;

    auto& d = slots_[idx];
    d.pos = pos;
    d.size = (wt == WeaponType::Rocket) ? 30.f : 4.f;
    d.opacity = 1.f;

    // Compute tangent/bitangent from normal
    const glm::vec3 up = (std::abs(normal.y) > 0.9f) ? glm::vec3{1.f, 0.f, 0.f} : glm::vec3{0.f, 1.f, 0.f};
    d.right = glm::normalize(glm::cross(normal, up));
    d.up = glm::cross(d.right, normal);
    d._p0 = 0.f;

    // UV region in a placeholder atlas (full texture = single decal for now)
    d.uvMin = {0.f, 0.f};
    d.uvMax = {1.f, 1.f};
}

void BulletHoleDecal::update(float dt)
{
    const uint32_t live = std::min(head_, k_max);
    for (uint32_t i = 0; i < live; ++i) {
        slots_[i].opacity = std::max(0.f, slots_[i].opacity - dt / 15.f);
    }
}
