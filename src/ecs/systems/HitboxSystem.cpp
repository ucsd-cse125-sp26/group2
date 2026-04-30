/// @file HitboxSystem.cpp
/// @brief Transform skeleton joint poses into world-space hitbox capsules.

#include "ecs/systems/HitboxSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RespawnTimer.hpp"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace systems
{

void updateHitboxes(Registry& registry, const HitboxRig& hitboxRig, float rigScale, float rigMeshMinY)
{
    // Remove stale hitboxes from dead/respawning entities (handles client side where handleDeath doesn't run).
    for (auto entity : registry.view<RespawnTimer>())
        registry.remove<HitboxInstance>(entity);

    auto view = registry.view<Position, JointMatrices>(entt::exclude<RespawnTimer>);
    view.each([&](entt::entity entity, const Position& pos, const JointMatrices& joints) {
        auto& instance = registry.get_or_emplace<HitboxInstance>(entity);
        instance.capsules.resize(hitboxRig.definitions.size());

        // Retrieve entity yaw from InputSnapshot (default 0 if absent).
        float yaw = 0.0f;
        if (const auto* inp = registry.try_get<InputSnapshot>(entity))
            yaw = inp->yaw;

        // Retrieve standing half-height for vertical offset computation.
        float halfHeight = 36.0f; // default standing
        if (const auto* shape = registry.try_get<CollisionShape>(entity))
            halfHeight = shape->halfExtents.y;

        // Vertical offset aligns the model's feet (meshMinY) with the bottom
        // of the collision AABB.  Matches the renderer's rend.translation.y.
        const float verticalOffset = -halfHeight - rigMeshMinY * rigScale;

        // Build entity world transform:
        //   translate(entityPos) * translate(0, verticalOffset, 0) * rotateY(yaw) * scale(rigScale)
        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);

        // Compose manually for efficiency (single 4x4).
        // Must match the renderer's transform: translate * mat4_cast(angleAxis(yaw, Y)) * scale.
        // glm::angleAxis(yaw, Y) produces columns:
        //   col0 = ( cos, 0, -sin)
        //   col1 = (   0, 1,    0)
        //   col2 = ( sin, 0,  cos)
        glm::mat4 worldTransform(1.0f);
        worldTransform[0] = glm::vec4(cosY * rigScale, 0.0f, -sinY * rigScale, 0.0f);
        worldTransform[1] = glm::vec4(0.0f, rigScale, 0.0f, 0.0f);
        worldTransform[2] = glm::vec4(sinY * rigScale, 0.0f, cosY * rigScale, 0.0f);
        worldTransform[3] = glm::vec4(pos.value.x, pos.value.y + verticalOffset, pos.value.z, 1.0f);

        for (size_t i = 0; i < hitboxRig.definitions.size(); ++i) {
            const auto& def = hitboxRig.definitions[i];
            auto& capsule = instance.capsules[i];
            capsule.region = def.region;

            if (def.boneIndex < 0 || static_cast<size_t>(def.boneIndex) >= joints.matrices.size()) {
                // Bone not resolved — place degenerate capsule at entity origin.
                capsule.pointA = pos.value;
                capsule.pointB = pos.value;
                capsule.radius = def.radius * rigScale;
                continue;
            }

            // Full transform: worldTransform * boneModelSpaceMatrix
            const glm::mat4 boneMat = worldTransform * joints.matrices[static_cast<size_t>(def.boneIndex)];

            // Transform capsule endpoints from bone-local space to world space.
            const glm::vec3 axisScaled = def.localAxis * def.halfHeight;
            capsule.pointA = glm::vec3(boneMat * glm::vec4(def.localOffset + axisScaled, 1.0f));
            capsule.pointB = glm::vec3(boneMat * glm::vec4(def.localOffset - axisScaled, 1.0f));
            capsule.radius = def.radius * rigScale;
        }
    });
}

} // namespace systems
