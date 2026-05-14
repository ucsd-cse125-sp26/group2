/// @file Sleep.hpp
/// @brief Body sleeping + constraint-island detection.
///
/// Bodies whose energy stays below threshold for `sleepTimerThreshold`
/// frames are flagged asleep (`RigidBody::isAsleep`) and skipped by the
/// solver / integrator until something wakes them — usually a new contact
/// from an awake body or an applied force/impulse.
///
/// Islands group contacts and joints into connected components of the
/// constraint graph: an island sleeps only when all its bodies sleep, so
/// e.g. a stack of boxes wakes all at once when the bottom one is kicked.
/// Phase 12 ships per-body sleep — island-level sleep is set up via the
/// `wakeIslandsOf(...)` helper that walks the contact graph.

#pragma once

#include "ecs/physics/ContactCache.hpp"
#include "ecs/registry/Registry.hpp"

namespace physics
{

struct SleepConfig
{
    /// @brief Linear velocity below which a body counts as "still".
    float linearThresh = 0.5f;

    /// @brief Angular velocity below which a body counts as "still".
    float angularThresh = 0.1f;

    /// @brief Frames of stillness before a body sleeps (default ~0.5 s at 128 Hz).
    uint16_t framesToSleep = 64;
};

/// @brief Update each body's sleep state from its current velocities.
/// Called once per tick after the solver has converged.
void updateSleep(Registry& registry, const SleepConfig& cfg);

/// @brief Wake every body that's currently in the same contact island
/// as `e`.  Walks the contact graph in `cache` using BFS; O(island size).
void wakeIslandOf(Registry& registry, const ContactCache& cache, entt::entity e);

/// @brief Wake a single body (e.g. for an explicit force).  Does NOT
/// propagate through the contact graph; use `wakeIslandOf` for that.
void wakeBody(Registry& registry, entt::entity e);

} // namespace physics
