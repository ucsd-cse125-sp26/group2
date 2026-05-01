/// @file RegistrySerialization.cpp
/// @brief Implementation of ECS registry serialization for network transport.

#include "RegistrySerialization.hpp"

#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/DeathInfo.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/components/WeaponState.hpp"
#include "entt/entity/fwd.hpp"
#include "network/RegistryArchive.hpp"

#include <array>
#include <cstdint>
#include <entt/entt.hpp>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

// PR-8 (server-perf): optional parallel-STL hooks for the per-type
// serialization fan-out. Header-only on the server build path; client
// TUs compile this file without the perf module on the include path,
// so we fall back to sequential there.
#if __has_include("perf/Parallel.hpp")
#include "perf/Parallel.hpp"
#define GROUP2_REGSER_HAS_PARALLEL 1
#else
#define GROUP2_REGSER_HAS_PARALLEL 0
#endif

namespace
{

template <typename S, typename A, typename... Cs>
void getAll(S& snapshot, A& archive, std::tuple<Cs...>*)
{
    (snapshot.template get<Cs>(archive), ...);
}

// PR-8 (server-perf): parallel per-type fan-out helper.
//
// Each call to `entt::snapshot.get<T>(archive)` is self-contained
// for type T — it writes the count of entities with T, then each
// (entity, component) pair into the archive. Different Ts touch
// disjoint storages in the registry, so reading them concurrently
// is safe as long as nothing is mutating those storages
// concurrently. broadcastRegistry runs on the game thread *after*
// all ECS systems have finished mutating, so the precondition
// holds.
//
// We allocate one OutputArchive per type, dispatch the
// serialisation as N independent jobs to the parallel-STL pool,
// and concatenate the per-type buffers in tuple order at the end.
// The receiving Loader walks the same tuple in the same order, so
// the wire bytes are identical to the sequential form.
template <typename Tuple, std::size_t... Is>
void serializeParallelImpl(const entt::registry& registry, OutputArchive& output, std::index_sequence<Is...> /*seq*/)
{
    constexpr std::size_t k_n = sizeof...(Is);
    std::array<OutputArchive, k_n> archives;

#if GROUP2_REGSER_HAS_PARALLEL
    std::array<std::size_t, k_n> indices = {Is...};
    // Per-type kernel — captures the shared registry by reference and
    // writes into its own archive slot. `entt::snapshot{registry}` is
    // a thin wrapper; constructing one per task is free.
    auto kernel = [&registry, &archives](std::size_t idx) {
        // Dispatch on idx → tuple element. Each tuple position has its
        // own kernel; we generate them via an inner index-sequence.
        // The fold-expression resolves the matching `Is` at compile
        // time; only one branch fires per kernel call.
        auto snap = entt::snapshot{registry};
        ((idx == Is ? (snap.template get<std::tuple_element_t<Is, Tuple>>(archives[Is]), 0) : 0), ...);
    };
    ::group2::perf::parallelFor(indices.begin(), indices.end(), kernel);
#else
    // Sequential fallback — same as the original `getAll` path, but
    // routed through the per-type-archive form so the merge step is
    // identical between the two builds.
    auto snap = entt::snapshot{registry};
    ((snap.template get<std::tuple_element_t<Is, Tuple>>(archives[Is])), ...);
#endif

    // Concatenate per-type buffers in tuple order. This is a sequential
    // pass, but each insertion is just a memcpy and the total size is
    // bounded by the snapshot wire size — typically 10s of KB.
    for (std::size_t i = 0; i < k_n; ++i) {
        output.buffer.insert(output.buffer.end(), archives[i].buffer.begin(), archives[i].buffer.end());
    }
}

template <typename Tuple>
void serializeParallel(const entt::registry& registry, OutputArchive& output)
{
    serializeParallelImpl<Tuple>(registry, output, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

} // namespace

namespace registry_serialization
{

// NOTE: this is where any component that should be sent to clients must be listed.
// The order of components in this tuple is the order they will be serialized in.
//
// PHASE 2: PlayerState was split into PlayerVisState (replicated, ~64 B) and
// PlayerSimState (server-only, ~150 B). Only PlayerVisState appears here —
// PlayerSimState stays on the server. This is the headline bandwidth win
// of Phase 2: the per-player on-the-wire share of the registry payload
// drops from ~280 B to ~64 B (≈4× reduction) before any other deltas /
// quantization land in Phase 4.
using Synced = std::tuple<entt::entity,
                          Position,
                          Velocity,
                          PlayerVisState,
                          CollisionShape,
                          WeaponState,
                          Health,
                          PlayerMatchStats,
                          Projectile,
                          BeamState,
                          ClientId,
                          DeathInfo,
                          RespawnTimer,
                          WeaponSpawner>;

std::vector<uint8_t> serialize(const entt::registry& registry)
{
    OutputArchive snapshotArchive;

    // PR-8 (server-perf): per-type parallel serialisation. With 14
    // component types in `Synced` and 16 cores available, each type
    // serialises in parallel into its own archive; we concat in
    // tuple order at the end. Pre-PR-8 the sequential
    // `getAll(snapshot, archive, Synced*)` form took 25-100 ms p99
    // at 300-500 entities and was the dominant CPU scope on
    // snapshot ticks.
    //
    // The fall-through (sequential) form is preserved on builds
    // without the perf module — the wire bytes are identical
    // because the merge step concatenates in the same order the
    // Loader expects.
    serializeParallel<Synced>(registry, snapshotArchive);

    std::vector<RemoteInputRecord> remoteInputs;
    if (const auto view = registry.view<const InputSnapshot>(); !view.empty()) {
        remoteInputs.reserve(view.size());
        view.each([&](const entt::entity entity, const InputSnapshot& input) {
            remoteInputs.push_back(RemoteInputRecord{.entity = entity, .input = input});
        });
    }

    OutputArchive packetArchive;
    const auto snapshotSize = static_cast<uint32_t>(snapshotArchive.buffer.size());
    packetArchive(snapshotSize);
    packetArchive.buffer.insert(
        packetArchive.buffer.end(), snapshotArchive.buffer.begin(), snapshotArchive.buffer.end());

    const auto remoteInputCount = static_cast<uint32_t>(remoteInputs.size());
    packetArchive(remoteInputCount);
    for (const RemoteInputRecord& record : remoteInputs) {
        packetArchive(record);
    }

    return std::move(packetArchive.buffer);
}

void Loader::apply(const uint8_t* data,
                   size_t size,
                   const std::optional<entt::entity> localPlayerServerEntity,
                   uint32_t* outServerAckedClientTick)
{
    if (outServerAckedClientTick)
        *outServerAckedClientTick = 0;

    InputArchive packetArchive(data, size);
    uint32_t snapshotSize = 0;
    packetArchive(snapshotSize);

    if (sizeof(snapshotSize) + static_cast<size_t>(snapshotSize) > size) {
        throw std::runtime_error("RegistrySerialization: invalid snapshot payload size");
    }

    InputArchive snapshotArchive(data + sizeof(snapshotSize), snapshotSize);
    getAll(loader, snapshotArchive, static_cast<Synced*>(nullptr));

    loader.orphans();

    const size_t remoteInputOffset = sizeof(snapshotSize) + static_cast<size_t>(snapshotSize);
    InputArchive remoteInputArchive(data + remoteInputOffset, size - remoteInputOffset);
    uint32_t remoteInputCount = 0;
    remoteInputArchive(remoteInputCount);

    entt::entity localPlayerEntity = entt::null;
    if (localPlayerServerEntity.has_value()) {
        localPlayerEntity = loader.map(*localPlayerServerEntity);
    }

    for (uint32_t i = 0; i < remoteInputCount; ++i) {
        RemoteInputRecord record;
        remoteInputArchive(record);

        const entt::entity entity = loader.map(record.entity);
        if (entity == entt::null || !registry.valid(entity)) {
            continue;
        }

        if (entity == localPlayerEntity) {
            // Don't overwrite the client's locally-stamped InputSnapshot —
            // that would clobber the input the client just sent for the
            // current predict tick. But DO read the server's tick field:
            // it tells us the most-recently-applied client tick the
            // server has processed, which Phase 5b reconciliation uses to
            // know where to start the input replay from.
            if (outServerAckedClientTick)
                *outServerAckedClientTick = record.input.tick;
            continue;
        }

        registry.emplace_or_replace<InputSnapshot>(entity, record.input);
    }
}

entt::entity Loader::map(entt::entity e) const
{
    return loader.map(e);
}

} // namespace registry_serialization
