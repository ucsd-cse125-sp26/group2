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

#include <cstdint>
#include <entt/entt.hpp>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace
{

template <typename S, typename A, typename... Cs>
void getAll(S& snapshot, A& archive, std::tuple<Cs...>*)
{
    (snapshot.template get<Cs>(archive), ...);
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

    auto snapshot = entt::snapshot{registry};
    getAll(snapshot, snapshotArchive, static_cast<Synced*>(nullptr));

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
