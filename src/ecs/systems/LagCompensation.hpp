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

#include "ecs/components/Hitbox.hpp"
#include "ecs/components/HitboxHistory.hpp"
#include "ecs/components/LagCompTarget.hpp"
#include "ecs/registry/Registry.hpp"

#include <cstdint>
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

    friend RewindHitboxesGuard rewindHitboxes(Registry&, entt::entity);
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
/// `shooter` itself is not rewound — `resolveHitscanHitbox` already
/// excludes the shooter from the player-hitbox raycast, so rewinding
/// their capsules would be wasted work. (And conceptually wrong: the
/// shot ray originates from the shooter's *current* position, not
/// their position N ticks ago.)
///
/// @param registry The server ECS registry.
/// @param shooter  Entity firing the hitscan. Read for `LagCompTarget`.
/// @return Guard whose destructor restores the original capsules.
inline RewindHitboxesGuard rewindHitboxes(Registry& registry, entt::entity shooter)
{
    RewindHitboxesGuard guard;

    const auto* target = registry.try_get<LagCompTarget>(shooter);
    if (target == nullptr || target->targetServerTick == 0)
        return guard; // no-op: no rewind requested for this shooter

    const uint32_t targetTick = target->targetServerTick;

    auto view = registry.view<HitboxInstance, HitboxHistory>();
    view.each([&](entt::entity entity, HitboxInstance& inst, const HitboxHistory& hist) {
        if (entity == shooter)
            return;

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
