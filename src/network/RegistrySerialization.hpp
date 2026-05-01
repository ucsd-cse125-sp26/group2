/// @file RegistrySerialization.hpp
/// @brief Serialize and deserialize the entt ECS registry for network replication.

#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "entt/entity/fwd.hpp"

#include <entt/entt.hpp>
#include <optional>
#include <vector>

/// @brief Utilities for serializing and deserializing the ECS registry over the network.
namespace registry_serialization
{

/// @brief A paired entity ID and input snapshot received from a remote client.
struct RemoteInputRecord
{
    entt::entity entity{entt::null};
    InputSnapshot input{};
};

/// @brief Serialize the full registry state into a byte buffer for transmission.
/// @param registry The source ECS registry.
/// @return A byte vector containing the serialized snapshot.
std::vector<uint8_t> serialize(const entt::registry& registry);

/// @brief Deserializes registry snapshots received from the server and applies them locally.
class Loader
{
public:
    /// @brief Construct a Loader bound to the given registry.
    /// @param registry The local ECS registry to update.
    explicit Loader(entt ::registry& registry) : registry(registry), loader(registry) {}

    /// @brief Apply a serialized registry snapshot to the local registry.
    /// @param data Pointer to the serialized data buffer.
    /// @param size Size of the data buffer in bytes.
    /// @param localPlayerServerEntity Optional server-side entity ID of the local player (excluded from remote input
    /// application).
    /// @param outServerAckedClientTick Optional out-param. If non-null,
    /// receives the `tick` field from the local player's `InputSnapshot`
    /// in the server's remote-input list — the most-recently-applied
    /// client input tick the server has processed. Used by Phase 5b
    /// reconciliation to know how far forward to replay client predicted
    /// inputs after the snapshot apply. Set to 0 if the local player has
    /// no input record in this snapshot (e.g. no inputs sent yet).
    void apply(const uint8_t* data,
               size_t size,
               std::optional<entt::entity> localPlayerServerEntity = std::nullopt,
               uint32_t* outServerAckedClientTick = nullptr);

    /// @brief Map a server-side entity ID to its local equivalent.
    /// @param e The server-side entity.
    /// @return The corresponding local entity, or entt::null if not mapped.
    [[nodiscard]] entt::entity map(entt::entity e) const;

private:
    entt::registry& registry;
    entt::continuous_loader loader;
};

} // namespace registry_serialization
