/// @file DeterminismHash.cpp
/// @brief FNV-1a state hash implementation.

#include "ecs/physics/DeterminismHash.hpp"

#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace physics::diag
{

namespace
{

constexpr uint64_t k_fnvOffset = 0xcbf29ce484222325ULL;
constexpr uint64_t k_fnvPrime = 0x100000001b3ULL;

uint64_t foldBytes(uint64_t h, const void* data, size_t bytes)
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= k_fnvPrime;
    }
    return h;
}

template <typename T>
uint64_t foldT(uint64_t h, const T& v)
{
    return foldBytes(h, &v, sizeof(T));
}

} // namespace

uint64_t hashPhysicsState(const Registry& registry) noexcept
{
    // Collect every entity that has a Position component (and therefore
    // counts as physics-relevant), then sort by stable id.  Hashing in
    // sorted order makes the result invariant to entt's archetype-internal
    // ordering, so two runs with different thread schedules still match.
    std::vector<entt::entity> all;
    auto each = registry.view<Position>();
    all.reserve(static_cast<size_t>(each.size()));
    for (auto e : each)
        all.push_back(e);

    std::sort(all.begin(), all.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) < entt::to_integral(b);
    });

    uint64_t h = k_fnvOffset;
    for (entt::entity e : all) {
        // Fold the entity id so missing/added entities flip the hash.
        const uint32_t id = entt::to_integral(e);
        h = foldT(h, id);

        if (const auto* pos = registry.try_get<Position>(e))
            h = foldT(h, pos->value);
        if (const auto* vel = registry.try_get<Velocity>(e))
            h = foldT(h, vel->value);
        if (const auto* ori = registry.try_get<Orientation>(e))
            h = foldT(h, ori->value);
        if (const auto* angVel = registry.try_get<AngularVelocity>(e))
            h = foldT(h, angVel->value);
        if (const auto* rb = registry.try_get<RigidBody>(e)) {
            h = foldT(h, rb->invMass);
            h = foldT(h, rb->isAsleep);
            h = foldT(h, rb->sleepCounter);
            // Accumulators are zeroed every tick — don't include them or
            // intra-tick observation windows would change the hash.
        }
    }
    return h;
}

} // namespace physics::diag
