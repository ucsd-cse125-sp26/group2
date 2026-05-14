/// @file CollisionEvents.hpp
/// @brief Per-tick collision-event queue.  Drained by gameplay code each
///        tick; populated by the trigger subsystem (and, later, by the
///        rigid-body contact solver in Phase 9).
///
/// The queue is thread-local on the write side (so parallel kernels that
/// emit events don't contend), and merged into a single per-tick frontbuffer
/// by `beginTick()`.  Same architecture as `physics::debug` so determinism
/// is preserved: events are written in entity-id order from a single thread,
/// or merged after a sim step in stable iteration order.

#pragma once

#include "ecs/registry/Registry.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <span>

namespace physics::events
{

/// @brief Kind of contact event.
enum class TriggerEventType : uint8_t
{
    Enter, ///< Entity overlapped a trigger this tick that wasn't overlapping last tick.
    Stay,  ///< Overlap persists across ticks (emitted every tick during overlap).
    Exit,  ///< Overlap ended this tick.
};

/// @brief One trigger-overlap event.  `trigger` and `entity` are stable ECS
/// entity handles; sentinel values are NOT used.
struct TriggerEvent
{
    entt::entity trigger{entt::null}; ///< The TriggerVolume entity.
    entt::entity entity{entt::null};  ///< The non-trigger entity overlapping it.
    TriggerEventType type;
};

/// @brief Start a fresh per-tick event window.  Clears the global front
/// buffer and drains every thread-local writer buffer.  Must be called
/// once per tick on the main thread BEFORE the trigger / contact systems
/// run.  Calling concurrently with `pushTriggerEvent` is UB.
void beginTick() noexcept;

/// @brief Append a trigger event to the current tick's queue.
/// Thread-safe (per-thread buffer; no locking on the hot path).
void pushTriggerEvent(const TriggerEvent& e) noexcept;

/// @brief Read-only snapshot of all events emitted since `beginTick()`.
/// Stable until the next `beginTick()`.
[[nodiscard]] std::span<const TriggerEvent> triggerEvents() noexcept;

} // namespace physics::events
