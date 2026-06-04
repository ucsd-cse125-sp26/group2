/// @file HitboxSystem.cpp
/// @brief Transform skeleton joint poses into world-space hitbox capsules.

#include "ecs/systems/HitboxSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RespawnTimer.hpp"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#if __has_include("perf/Parallel.hpp")
#include "perf/Parallel.hpp"
#define GROUP2_HITBOX_HAS_PARALLEL 1
#else
// Client-side build doesn't ship the server-perf parallel header.
// Sequential fallback keeps the system header-portable.
#define GROUP2_HITBOX_HAS_PARALLEL 0
#endif

#include <vector>

namespace systems
{

void updateHitboxes(Registry& registry, const HitboxRig& hitboxRig, float rigScale, float rigMeshMinY)
{
    // Remove stale hitboxes from dead/respawning entities (handles client side where handleDeath doesn't run).
    for (auto entity : registry.view<RespawnTimer>())
        registry.remove<HitboxInstance>(entity);

    // PR-3 (server-perf): the per-entity capsule transform is
    // embarrassingly parallel — each iteration reads its own Position /
    // JointMatrices / InputSnapshot / CollisionShape (read-only) and
    // writes its own HitboxInstance::capsules. We pre-emplace
    // HitboxInstance and pre-resize its `capsules` vector single-
    // threaded, then run the math in parallel on the resulting flat
    // entity vector.
    //
    // Pre-emplace avoids the entt component-pool growth race; the
    // pre-resize avoids per-thread vector-allocation contention on
    // the per-instance capsule storage.
    auto view = registry.view<Position, JointMatrices>(entt::exclude<RespawnTimer>);

    // Phase 1: pre-emplace + pre-resize, collect work.
    static thread_local std::vector<entt::entity> work;
    work.clear();
    for (auto entity : view) {
        auto& instance = registry.get_or_emplace<HitboxInstance>(entity);
        if (instance.capsules.size() != hitboxRig.definitions.size())
            instance.capsules.resize(hitboxRig.definitions.size());
        work.push_back(entity);
    }

    auto kernel = [&](entt::entity entity) {
        const auto& pos = registry.get<Position>(entity);
        const auto& joints = registry.get<JointMatrices>(entity);
        auto& instance = registry.get<HitboxInstance>(entity);

        // Retrieve entity yaw from InputSnapshot (default 0 if absent).
        float yaw = 0.0f;
        if (const auto* inp = registry.try_get<InputSnapshot>(entity))
            yaw = inp->yaw;

        // Retrieve standing half-height for vertical offset computation.
        float halfHeight = 36.0f; // default standing
        if (const auto* shape = registry.try_get<CollisionShape>(entity))
            halfHeight = shape->halfExtents.y;

        // Check whether this entity's gravity is inverted.
        bool gravFlipped = false;
        if (const auto* vis = registry.try_get<PlayerVisState>(entity))
            gravFlipped = vis->gravityFlipped;

        // Vertical offset aligns the model's feet with the collision AABB.
        // Normal gravity: feet at AABB bottom  →  offset = -(halfHeight + meshMinY * scale)
        // Flipped gravity: feet at AABB top    →  offset = +(halfHeight + meshMinY * scale)
        // Must match the renderer's Renderable.translation.y.
        const float verticalOffset =
            gravFlipped ? (halfHeight + rigMeshMinY * rigScale) : (-halfHeight - rigMeshMinY * rigScale);

        // Build entity world transform.  Must match the renderer's transform:
        //   translate * mat4_cast(orient) * scale
        //
        // Normal:  translate(pos + voff) * rotateY(yaw) * scale(s)
        // Flipped: translate(pos + voff) * rotateY(yaw) * rotateZ(π) * scale(s)
        //
        // The extra rotateZ(π) flips the skeleton upside-down so hitbox
        // capsules (head, torso, legs) land where the visual model is drawn.
        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);

        glm::mat4 worldTransform(1.0f);
        if (gravFlipped) {
            // rotateY(yaw) * rotateZ(π) * scale(s)
            // rotateZ(π) = diag(-1, -1, 1), so:
            //   col0 negated, col1 negated, col2 unchanged vs. normal.
            worldTransform[0] = glm::vec4(-cosY * rigScale, 0.0f, sinY * rigScale, 0.0f);
            worldTransform[1] = glm::vec4(0.0f, -rigScale, 0.0f, 0.0f);
            worldTransform[2] = glm::vec4(sinY * rigScale, 0.0f, cosY * rigScale, 0.0f);
        } else {
            // rotateY(yaw) * scale(s)
            worldTransform[0] = glm::vec4(cosY * rigScale, 0.0f, -sinY * rigScale, 0.0f);
            worldTransform[1] = glm::vec4(0.0f, rigScale, 0.0f, 0.0f);
            worldTransform[2] = glm::vec4(sinY * rigScale, 0.0f, cosY * rigScale, 0.0f);
        }
        worldTransform[3] = glm::vec4(pos.value.x, pos.value.y + verticalOffset, pos.value.z, 1.0f);

        for (size_t i = 0; i < hitboxRig.definitions.size(); ++i) {
            const auto& def = hitboxRig.definitions[i];
            auto& capsule = instance.capsules[i];
            capsule.region = def.region;

            if (def.boneIndex < 0 || static_cast<size_t>(def.boneIndex) >= joints.matrices.size()) {
                // Bone not resolved — place degenerate capsule at entity origin.
                capsule.pointA = pos.value;
                capsule.pointB = pos.value;
                capsule.radius = def.radius;
                continue;
            }

            // Full transform: worldTransform * boneModelSpaceMatrix
            const glm::mat4 boneMat = worldTransform * joints.matrices[static_cast<size_t>(def.boneIndex)];

            // HitboxDef dimensions are in WORLD units (scale-invariant). The bone
            // matrix carries the rig's model-space scale (rigScale AND any scale
            // baked into the joint hierarchy, e.g. an armature import scale), so we
            // can't just transform a local offset by it. Instead take the bone's
            // world POSITION from the matrix and its NORMALIZED axes (scale removed)
            // and lay the offset/half-height out along those axes in world units.
            const glm::vec3 bonePos = glm::vec3(boneMat[3]);
            const glm::mat3 boneRot(glm::normalize(glm::vec3(boneMat[0])),
                                    glm::normalize(glm::vec3(boneMat[1])),
                                    glm::normalize(glm::vec3(boneMat[2])));
            const glm::vec3 axisScaled = def.localAxis * def.halfHeight;
            capsule.pointA = bonePos + boneRot * (def.localOffset + axisScaled);
            capsule.pointB = bonePos + boneRot * (def.localOffset - axisScaled);
            capsule.radius = def.radius;
        }
    };

#if GROUP2_HITBOX_HAS_PARALLEL
    ::group2::perf::parallelFor(work.begin(), work.end(), kernel);
#else
    for (entt::entity e : work)
        kernel(e);
#endif
}

} // namespace systems
