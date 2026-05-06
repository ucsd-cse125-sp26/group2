/// @file ReconciliationSystem.hpp
/// @brief Replay client-stored inputs to recover prediction after a server snapshot apply.
///
/// Phase 5b: when an UPDATE_REGISTRY snapshot arrives, the local player's
/// `Position`/`Velocity`/`PlayerVisState`/etc. get rewritten from the
/// server's authoritative values. Those values represent the state at
/// some past client tick T (the most-recently-server-acked input tick),
/// not the current client predict tick. To get back to "where the client
/// thinks we should be right now" we replay every input the client
/// stored after T using the same MovementSystem + CollisionSystem the
/// server runs — yielding a state consistent with the current input,
/// just with any server-side correction (knockback, hit-stop, etc.)
/// folded in along the way.
///
/// We always replay (no "compare-and-skip" fast path). The cost is
/// ~RTT-in-ticks of `runMovement` calls per snapshot apply. At 50 ms RTT
/// and 128 Hz physics that's ~6 ticks of work, and `runMovement` for
/// just the local player on the client is microseconds. Cheap.
///
/// @note `PlayerSimState` is not replicated (server-only by design), so
/// the replay's starting `PlayerSimState` is whatever the client had
/// accumulated locally. This means subtle timer fields (coyote time,
/// jump cooldown, slide fatigue) can drift slightly between client and
/// server. Position/velocity stay correct via reconciliation; the drift
/// only matters for "feel" cases like edge-of-coyote-window jumps.
/// Phase 4b's owner-only stream would replicate `PlayerSimState` to
/// the local player and fix this.

#pragma once

#include "InputRingBuffer.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "systems/PredictionSystem.hpp"

#include <SDL3/SDL_log.h>

#include <cstdint>
#include <entt/entt.hpp>

namespace systems
{

/// @brief Replay stored inputs from `ackedTick + 1` through `currentTick`
///        on the local player, restoring its predicted state after a
///        snapshot apply rewrote it to the server-authoritative value.
///
/// @param registry      The client ECS registry. Local player's Position
///                      etc. should be the server's just-applied value.
/// @param ring          Input history ring; entries from earlier than
///                      `ackedTick` may have been overwritten and are
///                      not used.
/// @param ackedTick     The client tick the server has acknowledged
///                      processing through. The local player's current
///                      Position represents state as-of this tick.
/// @param currentTick   The client's current `clientPredictTick`. Replay
///                      runs `runMovement`+`runCollision` once per tick
///                      from `ackedTick + 1` through `currentTick`
///                      (inclusive).
/// @param dt            Physics delta (match server: 1/128 s).
/// @param world         World geometry (must match server).
inline void runReconciliation(Registry& registry,
                              const InputRingBuffer& ring,
                              uint32_t ackedTick,
                              uint32_t currentTick,
                              float dt,
                              const physics::WorldGeometry& world)
{
    if (currentTick <= ackedTick)
        return; // already up to date — server state is the latest predicted state.

    // Find the local player. Reconciliation is meaningless without one.
    auto localView = registry.view<LocalPlayer, InputSnapshot>();
    auto it = localView.begin();
    if (it == localView.end())
        return;
    const entt::entity local = *it;

    // Replay forward, one physics tick per stored input.
    int replayed = 0;
    int missing = 0;
    for (uint32_t t = ackedTick + 1; t <= currentTick; ++t) {
        const InputSnapshot* stored = ring.find(t);
        if (!stored) {
            // Ring overflow or we never sent for this tick — skip. The
            // gap means our prediction for this tick is lost, but
            // continuity is preserved (next stored input still applies).
            ++missing;
            continue;
        }
        registry.replace<InputSnapshot>(local, *stored);
        // runMovement+runCollision for the local player only. The
        // PlayerSimState filter (server-only component, only present on
        // the local player on the client) narrows the iteration
        // automatically — no per-entity filter needed.
        runMovement(registry, dt, world);
        runCollision(registry, dt, world);
        ++replayed;
    }

    if (missing > 0) {
        // Diagnostic: a non-trivial gap in the ring is unusual at sane
        // RTTs (256-tick capacity covers ~2 s of replay). Log so we can
        // notice if RTT or stalls drive the buffer beyond capacity.
        SDL_Log("[reconcile] %d ticks missing from input ring (replayed %d, range [%u..%u])",
                missing,
                replayed,
                ackedTick + 1,
                currentTick);
    }
}

} // namespace systems
