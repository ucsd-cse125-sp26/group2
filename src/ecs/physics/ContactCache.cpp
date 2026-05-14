/// @file ContactCache.cpp
/// @brief Implementation of the persistent contact cache.

#include "ecs/physics/ContactCache.hpp"

#include <algorithm>

namespace physics
{

uint64_t ContactCache::pairKey(entt::entity a, entt::entity b) noexcept
{
    auto ai = entt::to_integral(a);
    auto bi = entt::to_integral(b);
    if (ai > bi)
        std::swap(ai, bi);
    return (static_cast<uint64_t>(ai) << 32) | static_cast<uint64_t>(bi);
}

ContactManifold& ContactCache::merge(const ContactManifold& incoming)
{
    const uint64_t key = pairKey(incoming.a, incoming.b);
    auto [it, inserted] = cache_.try_emplace(key);

    Entry& entry = it->second;
    if (inserted) {
        // No cached manifold — fresh contact, zero impulses already.
        entry.manifold = incoming;
        entry.lastTouchedFrame = currentFrame_;
        return entry.manifold;
    }

    // Warm-start: copy accumulated impulses from cached points whose feature
    // ids match incoming ones.  Box2D-style: for each new point, search for
    // a cached point with the same feature id.  At most 4×4 = 16 compares.
    const ContactManifold cached = entry.manifold;
    entry.manifold = incoming;
    for (size_t i = 0; i < static_cast<size_t>(entry.manifold.pointCount); ++i) {
        const auto& newId = entry.manifold.points[i].featureId;
        if (newId.value == 0xFFFFFFFFu)
            continue;
        for (size_t j = 0; j < static_cast<size_t>(cached.pointCount); ++j) {
            if (cached.points[j].featureId == newId) {
                entry.manifold.points[i].normalImpulse = cached.points[j].normalImpulse;
                entry.manifold.points[i].tangentImpulse[0] = cached.points[j].tangentImpulse[0];
                entry.manifold.points[i].tangentImpulse[1] = cached.points[j].tangentImpulse[1];
                break;
            }
        }
    }
    entry.lastTouchedFrame = currentFrame_;
    return entry.manifold;
}

void ContactCache::endFrame() noexcept
{
    // Erase entries that weren't refreshed this frame — they no longer
    // represent overlapping pairs.  Using erase_if + lambda for stable
    // performance in MSVC and libstdc++.
    std::erase_if(cache_,
                  [this](const auto& kv) { return kv.second.lastTouchedFrame != currentFrame_; });
    ++currentFrame_;
}

void ContactCache::clear() noexcept
{
    cache_.clear();
    currentFrame_ = 0;
}

} // namespace physics
