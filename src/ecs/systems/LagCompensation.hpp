/// @file LagCompensation.hpp
/// @brief RAII guard for hitbox-rewind lag compensation.
///
/// When the server processes an attacker's hitscan input,
/// `rewindHitboxes(registry, shooter)` swaps every other player's
/// `HitboxInstance::capsules` for the historical snapshot that was
/// current on the attacker's screen at fire time (looked up via
/// `HitboxHistory`). On scope exit the guard's destructor restores
/// the live capsules so subsequent simulation work isn't affected.
///
/// The rewind target is read from a per-shooter `LagCompTarget`
/// component populated by the server's lag-comp scheduler each tick:
/// `targetServerTick = currentServerTick - clamp(rttMs/2 → ticks,
/// 0, k_maxLagCompTicks)`. Zero-target = no rewind (used for new
/// connections that haven't completed a ping round trip and for
/// loopback / sub-tick RTTs).
///
/// Cross-binary behaviour
/// ----------------------
/// `LagCompensation.hpp` is shared between server and client TUs
/// because `WeaponSystem.cpp` (which calls `rewindHitboxes`) is
/// shared. On the client, no entity ever has a `LagCompTarget` or
/// `HitboxHistory` component — the rewind helper short-circuits to a
/// no-op guard. The same code path runs in both binaries; only the
/// server populates the inputs that make it do real work.
///
/// Design notes
/// ------------
/// - The guard is move-only. Copy would silently double-restore.
/// - The destructor must be exception-safe so a throwing raycast
///   doesn't skip the restore. Today no raycast throws, but the
///   contract is cheap to honour.
/// - The "find best sample" is O(k_capacity) = O(32). With ~30
///   players × ~12 capsules each, the per-hitscan rewind is well
///   under the runWeapon budget.

#pragma once

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/HitboxHistory.hpp"
#include "ecs/components/LagCompTarget.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/registry/Registry.hpp"

#include <cstdint>
#include <glm/common.hpp> // glm::abs
#include <glm/vec3.hpp>
#include <utility>
#include <vector>

namespace systems
{

/// @brief RAII handle that restores hitbox capsules on scope exit
/// after a `rewindHitboxes` call swapped them for historical samples.
///
/// Default-constructed instances own nothing and do nothing on
/// destruction — used for shooters with no `LagCompTarget`, no
/// `HitboxHistory` neighbours, or zero rewind. Populated instances
/// hold one `(entity, capsules)` pair per rewound entity; the
/// destructor moves each saved capsule vector back into the entity's
/// live `HitboxInstance`.
class RewindHitboxesGuard
{
public:
    RewindHitboxesGuard() = default;

    ~RewindHitboxesGuard() noexcept { restore(); }

    RewindHitboxesGuard(const RewindHitboxesGuard&) = delete;
    RewindHitboxesGuard& operator=(const RewindHitboxesGuard&) = delete;

    RewindHitboxesGuard(RewindHitboxesGuard&& other) noexcept
        : registry_(other.registry_), saved_(std::move(other.saved_))
    {
        other.registry_ = nullptr;
    }

    RewindHitboxesGuard& operator=(RewindHitboxesGuard&& other) noexcept
    {
        if (this != &other) {
            restore();
            registry_ = other.registry_;
            saved_ = std::move(other.saved_);
            other.registry_ = nullptr;
        }
        return *this;
    }

private:
    /// @brief Walk `saved_` and move each stored capsules vector back
    /// into its entity's `HitboxInstance`. No-op when `registry_` is
    /// null or `saved_` is empty.
    void restore() noexcept
    {
        if (registry_ == nullptr)
            return;
        for (auto& [entity, capsules] : saved_) {
            if (!registry_->valid(entity))
                continue;
            if (auto* inst = registry_->try_get<HitboxInstance>(entity))
                inst->capsules = std::move(capsules);
        }
        saved_.clear();
        registry_ = nullptr;
    }

    Registry* registry_ = nullptr;
    std::vector<std::pair<entt::entity, std::vector<WorldCapsule>>> saved_;

    friend RewindHitboxesGuard
    rewindHitboxes(Registry&, entt::entity, const glm::vec3*, const glm::vec3*, float, float);
};

/// @brief Rewind every other player's hitbox capsules to where they
/// were on `shooter`'s screen at fire time.
///
/// Reads `LagCompTarget` from `shooter`. If absent or zero, returns a
/// no-op guard immediately (the common case for client-side and for
/// sub-tick-RTT shooters). Otherwise walks every entity with both
/// `HitboxInstance` and `HitboxHistory`, finds the most recent
/// history sample whose `tick` is `≤ targetServerTick`, swaps the
/// live capsules for the historical ones, and stashes the originals
/// in the returned guard for restore-on-destruction.
///
/// PR-5 (server-perf): two overloads.
///   - The unfiltered form (kept for compatibility) rewinds every
///     player. O(N) per shot. Pre-PR-5 measurements at 200 bots
///     during fire bursts: this dominated the `weapon` scope's
///     6.29 ms p99 — 25 shots × 200 candidate rewinds × ~1–5 µs
///     each ≈ 5–25 ms / sec.
///   - The ray-filtered form `rewindHitboxes(registry, shooter,
///     origin, direction, maxDistance)` adds a broad-phase ray-vs-
///     AABB test BEFORE rewinding each candidate. Players whose
///     bounding box doesn't intersect the shot ray are skipped —
///     no ring scan, no capsule swap. The AABB test costs ~10 ns
///     and prunes >95 % of candidates for typical shot geometry.
///
/// `shooter` itself is not rewound — `resolveHitscanHitbox` already
/// excludes the shooter from the player-hitbox raycast.
///
/// @param registry The server ECS registry.
/// @param shooter  Entity firing the hitscan. Read for `LagCompTarget`.
/// @return Guard whose destructor restores the original capsules.
inline RewindHitboxesGuard rewindHitboxes(Registry& registry,
                                          entt::entity shooter,
                                          const glm::vec3* rayOrigin = nullptr,
                                          const glm::vec3* rayDirection = nullptr,
                                          float rayMaxDistance = 0.0f,
                                          float bulletRadius = 0.0f)
{
    RewindHitboxesGuard guard;

    const auto* target = registry.try_get<LagCompTarget>(shooter);
    if (target == nullptr || target->targetServerTick == 0)
        return guard; // no-op: no rewind requested for this shooter

    const uint32_t targetTick = target->targetServerTick;
    const bool haveFilter = rayOrigin != nullptr && rayDirection != nullptr && rayMaxDistance > 0.0f;

    // PR-24 (broad-phase fix): how far back in time we're rewinding,
    // in seconds.  The pre-PR-24 broad-phase filter tested ray vs the
    // entity's LIVE AABB (live position ± live halfExtents).  At
    // sprint speed (~700 u/s) and 100 ms RTT the entity has moved
    // ~70 u between the rewind tick and now — well outside the 32 u
    // X/Z width of the default player AABB.  Result: fast-moving
    // targets whose live position was OFF the ray but whose
    // historical position was ON the ray got broad-phase REJECTED.
    // Their capsules stayed LIVE (no rewind), so the raycast tested
    // against the unrewound capsules and reported a miss; the user
    // saw the shot-debug visualizer's red capsules at the LIVE
    // position, identical to "no rollback at all".  Original PR-5
    // assumed 72 u half-extent — that's our Y, but X/Z is only 16 u.
    //
    // Fix: dilate the broad-phase AABB by `|velocity| × lagWindow` on
    // each axis to cover the full range of positions the entity could
    // have occupied during the rewind window.  Cheap (one mul + one
    // add) and rarely false-rejects even at sprint+200ms.
    const float lagWindowSec = static_cast<float>(target->lagTicks) / 128.0f;

    auto view = registry.view<HitboxInstance, HitboxHistory>();
    view.each([&](entt::entity entity, HitboxInstance& inst, const HitboxHistory& hist) {
        if (entity == shooter)
            return;

        // PR-5/PR-24: ray-AABB broad-phase pre-filter, dilated by
        // possible motion over the rewind window so fast-moving
        // entities aren't false-rejected (see comment above).
        if (haveFilter) {
            const auto* pos = registry.try_get<Position>(entity);
            const auto* shape = registry.try_get<CollisionShape>(entity);
            if (pos != nullptr && shape != nullptr) {
                glm::vec3 expand = shape->halfExtents;
                if (const auto* vel = registry.try_get<Velocity>(entity)) {
                    expand += glm::abs(vel->value) * lagWindowSec;
                }
                // Match the inflated narrow-phase hit shape ("cylinder hitreg"):
                // dilate the broad-phase AABB by the bullet radius so a thick
                // shot that grazes this player isn't pruned before rewind.
                expand += glm::vec3{bulletRadius};
                const physics::WorldAABB bounds{
                    .min = pos->value - expand,
                    .max = pos->value + expand,
                };
                float aabbDist = rayMaxDistance;
                glm::vec3 aabbNormal{0.0f};
                if (!physics::raycastAABB(*rayOrigin, *rayDirection, bounds, rayMaxDistance, aabbDist, aabbNormal)) {
                    return; // skip rewind for this player — ray doesn't reach their motion-extruded AABB
                }
            }
        }

        // Linear scan over the ring (≤ 32 slots). Find the sample
        // with the latest tick that's still ≤ targetTick. Empty slots
        // (tick == 0) and slots newer than targetTick are skipped.
        const HitboxHistorySample* best = nullptr;
        for (std::size_t i = 0; i < hist.count; ++i) {
            const auto& sample = hist.ring[i];
            if (sample.tick == 0)
                continue;
            if (sample.tick > targetTick)
                continue;
            if (best == nullptr || sample.tick > best->tick)
                best = &sample;
        }
        if (best == nullptr)
            return; // no usable historical sample (target predates the ring)

        // Save the live capsules (move out, no copy) and copy the
        // historical sample in. Move-out leaves `inst.capsules`
        // empty; the assignment from `best->capsules` repopulates
        // it. The destructor move-restores in reverse.
        guard.saved_.emplace_back(entity, std::move(inst.capsules));
        inst.capsules = best->capsules;
    });

    if (!guard.saved_.empty())
        guard.registry_ = &registry;

    return guard;
}

} // namespace systems
