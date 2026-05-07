/// @file RegistrySerialization.cpp
/// @brief Implementation of ECS registry serialization for network transport.

#include "RegistrySerialization.hpp"

#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/DeathInfo.hpp"
#include "ecs/components/FireField.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerColor.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/components/RespawnPoint.hpp"
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
// PR-29: AnimSnapshot is in the synced set so the client can render
// remote players' animation state at server-authoritative `timeRatio`,
// eliminating the residual ~0.4-median anim-state drift PR-27a's
// telemetry caught (server animator at 128 Hz, client at 30 Hz —
// they advanced at the same total rate but the per-clip start-time
// offset persisted for the lifetime of each clip).  Wire cost:
// `5 × (1 + 4 + 4) = 45 B / player / snapshot`, ~46 KB/s at 16 players
// × 128 Hz before delta encoding.  In practice timeRatio changes
// every tick but `clipIdRaw + weight` are stable, so PR-10's RLE
// delta encoder compresses the steady-state contribution to roughly
// the timeRatio bytes (~20 B / player / tick) → ~20 KB/s post-delta.
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
                          WeaponSpawner,
                          RespawnPoint,
                          AnimSnapshot,
                          FireField,
                          PlayerColor>;

// ── PR-10: snapshot delta encoding ──────────────────────────────────────

std::vector<uint8_t> encodeDelta(const std::vector<uint8_t>& baseline, const std::vector<uint8_t>& current)
{
    std::vector<uint8_t> patch;
    if (baseline.size() != current.size()) {
        // Caller contract violation; return empty patch and let the
        // caller fall back to a full snapshot. Defensive only — server
        // already guards this.
        return patch;
    }

    // Worst-case patch size = current.size() + a few dozen bytes of
    // headers. Reserving once avoids reallocation churn.
    patch.reserve(current.size() + 16);

    const std::size_t n = current.size();
    std::size_t i = 0;
    while (i < n) {
        // Run of unchanged bytes.
        std::size_t skipStart = i;
        while (i < n && current[i] == baseline[i])
            ++i;
        const auto skipLen = static_cast<std::uint32_t>(i - skipStart);

        // Run of changed bytes.
        std::size_t copyStart = i;
        while (i < n && current[i] != baseline[i])
            ++i;
        const auto copyLen = static_cast<std::uint32_t>(i - copyStart);

        // Skip header.
        const auto* sl = reinterpret_cast<const std::uint8_t*>(&skipLen);
        patch.insert(patch.end(), sl, sl + sizeof(skipLen));
        // Copy header.
        const auto* cl = reinterpret_cast<const std::uint8_t*>(&copyLen);
        patch.insert(patch.end(), cl, cl + sizeof(copyLen));
        // Copy bytes.
        if (copyLen > 0) {
            patch.insert(patch.end(),
                         current.begin() + static_cast<std::ptrdiff_t>(copyStart),
                         current.begin() + static_cast<std::ptrdiff_t>(copyStart + copyLen));
        }
    }

    return patch;
}

std::vector<uint8_t>
applyDelta(const std::vector<uint8_t>& baseline, const uint8_t* patch, std::size_t patchSize, std::size_t outputSize)
{
    std::vector<uint8_t> output;
    if (baseline.size() != outputSize)
        return output; // size mismatch → cannot apply

    output.assign(baseline.begin(), baseline.end());

    std::size_t pos = 0;
    std::size_t patchPos = 0;
    while (pos < outputSize) {
        if (patchPos + 2 * sizeof(std::uint32_t) > patchSize) {
            // Truncated patch — bail with empty output (drop packet).
            return std::vector<uint8_t>{};
        }
        std::uint32_t skipLen = 0;
        std::uint32_t copyLen = 0;
        std::memcpy(&skipLen, patch + patchPos, sizeof(skipLen));
        patchPos += sizeof(skipLen);
        std::memcpy(&copyLen, patch + patchPos, sizeof(copyLen));
        patchPos += sizeof(copyLen);

        // Skip range copies from baseline (already there in `output`).
        pos += skipLen;

        if (pos + copyLen > outputSize || patchPos + copyLen > patchSize)
            return std::vector<uint8_t>{}; // malformed

        if (copyLen > 0) {
            std::memcpy(output.data() + pos, patch + patchPos, copyLen);
            patchPos += copyLen;
            pos += copyLen;
        }
    }
    return output;
}

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
