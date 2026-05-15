/// @file Sleep.cpp
/// @brief Sleep + island wake propagation implementation.

#include "ecs/physics/Sleep.hpp"

#include "ecs/components/Orientation.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"

#include <glm/geometric.hpp>
#include <unordered_set>
#include <vector>

namespace physics
{

void updateSleep(Registry& registry, const SleepConfig& cfg)
{
    auto view = registry.view<RigidBody, Velocity>();
    for (auto e : view) {
        RigidBody& rb = view.get<RigidBody>(e);
        if (rb.invMass <= 0.0f)
            continue; // static — already inert

        const Velocity& vel = view.get<Velocity>(e);
        const auto* angVel = registry.try_get<AngularVelocity>(e);

        const float linMag2 = glm::dot(vel.value, vel.value);
        const float angMag2 = (angVel != nullptr) ? glm::dot(angVel->value, angVel->value) : 0.0f;

        const bool stillEnough = linMag2 < cfg.linearThresh * cfg.linearThresh &&
                                 angMag2 < cfg.angularThresh * cfg.angularThresh;

        if (stillEnough) {
            if (rb.sleepCounter < UINT16_MAX)
                ++rb.sleepCounter;
            if (rb.sleepCounter >= cfg.framesToSleep)
                rb.isAsleep = true;
        } else {
            rb.sleepCounter = 0;
            rb.isAsleep = false;
        }
    }
}

void wakeBody(Registry& registry, entt::entity e)
{
    if (auto* rb = registry.try_get<RigidBody>(e)) {
        rb->isAsleep = false;
        rb->sleepCounter = 0;
    }
}

void wakeIslandOf(Registry& registry, const ContactCache& cache, entt::entity start)
{
    // Build adjacency on the fly from the contact cache: entity → entities
    // it's currently touching.  BFS from `start`, waking everything reached.
    std::unordered_set<entt::entity> visited;
    std::vector<entt::entity> queue;
    queue.push_back(start);
    visited.insert(start);

    while (!queue.empty()) {
        const entt::entity cur = queue.back();
        queue.pop_back();
        wakeBody(registry, cur);

        for (const auto& [key, entry] : cache) {
            const ContactManifold& mf = entry.manifold;
            entt::entity other = entt::null;
            if (mf.a == cur)
                other = mf.b;
            else if (mf.b == cur)
                other = mf.a;
            else
                continue;
            if (other == entt::null)
                continue;
            if (visited.insert(other).second)
                queue.push_back(other);
        }
    }
}

} // namespace physics
