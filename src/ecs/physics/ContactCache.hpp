/// @file ContactCache.hpp
/// @brief Per-pair contact manifold cache for warm-starting the solver.
///
/// Each frame:
///   1. Narrow phase produces a fresh manifold per overlapping pair.
///   2. `merge()` is called for each pair; if a cached manifold for the
///      same pair exists, accumulated normal/tangent impulses are copied
///      from cached points whose feature id matches.  Otherwise the new
///      points start with zero impulse.
///   3. After the solver runs and applies impulses, the (now-warmed)
///      manifolds are stored back into the cache for next frame.
///   4. `endFrame()` removes any cached manifolds that weren't refreshed
///      this frame (pair is no longer in contact).

#pragma once

#include "ecs/physics/ContactManifold.hpp"

#include <cstdint>
#include <unordered_map>

namespace physics
{

class ContactCache
{
public:
    /// @brief Canonical 64-bit key for an ordered pair (a, b) — undirected,
    /// so swapping a and b yields the same key.
    [[nodiscard]] static uint64_t pairKey(entt::entity a, entt::entity b) noexcept;

    /// @brief Look up the cached manifold for (a, b), copy accumulated
    /// impulses from any matching feature ids, then store `incoming`
    /// as the new cache entry.  Returns the in-cache reference (already
    /// warm-started) for the solver to consume.
    ContactManifold& merge(const ContactManifold& incoming);

    /// @brief Drop any cached manifolds that weren't merged this frame.
    /// MUST be called once per tick after every collision pair has been
    /// merged.
    void endFrame() noexcept;

    /// @brief Iterate every live cached manifold (e.g. for the solver).
    [[nodiscard]] auto begin() { return cache_.begin(); }
    [[nodiscard]] auto end() { return cache_.end(); }
    [[nodiscard]] auto begin() const { return cache_.begin(); }
    [[nodiscard]] auto end() const { return cache_.end(); }
    [[nodiscard]] size_t size() const noexcept { return cache_.size(); }
    void clear() noexcept;

private:
    struct Entry
    {
        ContactManifold manifold;
        uint32_t lastTouchedFrame = 0;
    };

    std::unordered_map<uint64_t, Entry> cache_;
    uint32_t currentFrame_ = 0;
};

} // namespace physics
