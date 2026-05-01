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

// ── PR-10 (server-perf): snapshot delta encoding ────────────────────────
//
// Two helpers pair to give a simple bytewise diff against a prior
// snapshot.  Format:
//   [skip:u32] [copy:u32] [copy_bytes : u8 × copy] (repeated until output
//                                                   covers `current.size()`)
// The decoder walks the same triples, copying `skip` bytes from the
// baseline and `copy` bytes from the patch into the output buffer.
//
// Caller contract: only emit a delta when `baseline.size() ==
// current.size()`. Different sizes mean the entt entity-list section
// shifted; the sequential skip/copy form would mis-align. Server
// falls back to a full snapshot in that case.
//
// `encodeDelta` returns the patch bytes only — callers prepend the
// `[currentTick:u32] [fromTick:u32] [size:u32]` wire header outside.

/// @brief Compute an RLE byte-diff patch from `baseline` to `current`.
///
/// Both buffers MUST have the same size; the returned patch reconstructs
/// `current` from a copy of `baseline` via `applyDelta`. The patch is
/// always smaller than `current` only when many bytes are unchanged;
/// callers should compare patch size to full size before sending.
///
/// @param baseline  Bytes the receiver currently holds.
/// @param current   Bytes the receiver should arrive at.
/// @return Patch bytes (possibly empty if both inputs are byte-identical).
std::vector<uint8_t> encodeDelta(const std::vector<uint8_t>& baseline, const std::vector<uint8_t>& current);

/// @brief Reconstruct `current` from `baseline` + patch.
///
/// @param baseline  Receiver's stored baseline (size must match
///                  `outputSize`).
/// @param patch     The bytes returned by a prior `encodeDelta`.
/// @param outputSize Expected size of the reconstructed buffer (passed
///                  on the wire as the third u32 of the delta header).
/// @return Reconstructed buffer of size `outputSize`, or empty on parse
///         error (truncated patch, malformed offsets).
std::vector<uint8_t>
applyDelta(const std::vector<uint8_t>& baseline, const uint8_t* patch, std::size_t patchSize, std::size_t outputSize);

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
