/// @file LagCompensation.hpp
/// @brief RAII guard for hitbox-rewind lag compensation (Phase 6 stub).
///
/// The guard's eventual job: when the server processes an attacker's
/// hitscan input, swap each potential target's `HitboxInstance::capsules`
/// for the historical snapshot that was current on the attacker's screen
/// at fire time (looked up via `HitboxHistory`). On scope exit the guard
/// restores the live capsules so subsequent simulation work isn't
/// affected.
///
/// Today the guard is a no-op stub: `rewindHitboxes` returns a default-
/// constructed guard that holds no state and restores nothing on
/// destruction. Inserting the call site at the top of every server-side
/// hitscan path now means the actual rewind is a single function-body
/// edit later, with zero risk of forgetting a path. The plan calls this
/// the "no-op flip ready" state.
///
/// Design notes
/// - The guard is move-only. Copy would silently double-restore.
/// - The guard's destructor must be exception-safe; we don't want a
///   throwing raycast to skip the restore (today none throw, but the
///   contract is cheap to honour).
/// - On the client this header still compiles and runs because the
///   guard is header-only and the future-implementation `view<HitboxHistory>`
///   query will simply find nothing — client entities never get a
///   `HitboxHistory` component.

#pragma once

#include "ecs/components/Hitbox.hpp"
#include "ecs/registry/Registry.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace systems
{

/// @brief RAII handle that (will, in a future PR) restores hitbox
/// capsules on scope exit after a rewind.
///
/// Default-constructed instances own nothing and do nothing on
/// destruction — that's the Phase 6 stub state. The intended future
/// shape stores `(entity, savedCapsules)` pairs and writes them back
/// in the destructor.
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
    /// @brief Walk `saved_` and copy each stored capsules vector back
    /// into its entity's `HitboxInstance`. No-op if `registry_` is null
    /// or `saved_` is empty (i.e. Phase 6 default-constructed guards).
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

    // Future-implementation factory builds populated guards through
    // direct field access; declare the friend here to avoid leaking
    // state via setters.
    friend RewindHitboxesGuard rewindHitboxes(Registry&, uint32_t);
};

/// @brief Build a rewind guard scoped to `targetTick`.
///
/// Phase 6: returns a default-constructed (no-op) guard regardless of
/// inputs. The signature matches the future shape so call sites land
/// correctly today and require no edits when the implementation flips.
///
/// Future: walks every entity that has both `HitboxInstance` and
/// `HitboxHistory`, finds the historical sample with tick closest to
/// (and not exceeding) `targetTick`, swaps the live `capsules` for the
/// historical ones, and stashes the originals in the returned guard
/// for restore-on-destruction.
///
/// @param registry   The server ECS registry.
/// @param targetTick Server tick to rewind to (typically
///                   `currentServerTick - oneWayLagTicks(attacker)`).
inline RewindHitboxesGuard rewindHitboxes(Registry& registry, uint32_t targetTick)
{
    (void)registry;
    (void)targetTick;
    return {};
}

} // namespace systems
