/// @file PredictionSystem.hpp
/// @brief Client-side prediction: run movement+collision locally each tick.
///
/// Phase 5b: instead of waiting ~1 RTT for the server to apply our input
/// and ship the resulting position back, the client runs the same
/// MovementSystem + CollisionSystem code locally on every physics tick
/// using its own input. Our local position updates immediately — the
/// renderer sees ~0 ms of input → camera latency instead of the ~31 ms +
/// RTT/2 round-trip that pure server-authoritative replication imposes.
///
/// **How "only the local player" filtering happens automatically.**
/// `runMovement` requires `<Position, Velocity, PlayerVisState,
/// PlayerSimState, CollisionShape, InputSnapshot>` (with `RespawnTimer`
/// excluded). On the client, `PlayerSimState` is **not replicated** —
/// it's a server-only component. We emplace it on the local player
/// when the connection completes; remote players have only
/// `PlayerVisState`. So the view's filter naturally narrows to just
/// the local player. No entity-filter parameter on `runMovement` needed.
///
/// **Reconciliation.** When the server's snapshot arrives ack'ing client
/// tick T, the position in that snapshot represents
/// `state-after-applying-our-input-T`. The renderer is currently showing
/// our prediction at tick `currentPredictTick > T`. We snap to the
/// server's state and replay inputs T+1..currentPredictTick from the
/// `InputRingBuffer`, landing back at the correct predicted state. See
/// runReconciliation in ReconciliationSystem.hpp.

#pragma once

#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"

namespace systems
{

/// @brief Run one tick of client-side prediction for the local player.
///
/// Just calls the shared `runMovement` and `runCollision` against the
/// local registry. The View filter (PlayerSimState) ensures only the
/// local player is processed on the client. Server's runMovement still
/// processes every player in the same single call.
///
/// @param registry  The client ECS registry.
/// @param dt        Fixed physics delta (must match server's dt for
///                  prediction parity — typically 1/128 s).
/// @param world     Active world geometry. Must match the server's world
///                  exactly or prediction diverges.
inline void runPrediction(Registry& registry, float dt, const physics::WorldGeometry& world)
{
    runMovement(registry, dt, world);
    runCollision(registry, dt, world);
}

} // namespace systems
