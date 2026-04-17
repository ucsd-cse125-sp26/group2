/// @file Client.hpp
/// @brief TCP client for connecting to the game server.

#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/MessageStream.hpp"
#include "network/RegistrySerialization.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <entt/entt.hpp>
#include <optional>

/// @brief Live network statistics updated each frame.
struct NetworkStats
{
    float rttMs = 0.0f;                 ///< Latest round-trip time (ms).
    float avgRttMs = 0.0f;              ///< Exponential moving average RTT (ms).
    uint64_t bytesRecvTotal = 0;        ///< Total bytes received since connection.
    uint64_t bytesSentTotal = 0;        ///< Total bytes sent since connection.
    float recvBytesPerSec = 0.0f;       ///< Receive bandwidth (bytes/sec, smoothed).
    float sendBytesPerSec = 0.0f;       ///< Send bandwidth (bytes/sec, smoothed).
    uint32_t registryUpdateSize = 0;    ///< Last registry update payload size (bytes).
    float registryUpdatesPerSec = 0.0f; ///< Registry updates received per second.
};

/// @brief TCP stream client — sends input to the server and receives state updates.
class Client
{
public:
    using LocalPlayerReadyFn = std::function<void(entt::entity localEntity)>;

    /// @brief Create the TCP socket and connect to the server.
    /// @param addr  Hostname or IP address of the server.
    /// @param port  TCP port the server is listening on.
    /// @return False on socket creation or DNS failure.
    bool init(const char* addr, Uint16 port);

    /// @brief Close the socket and release the resolved address.
    void shutdown();

    /// @brief Send a raw message to the server.
    /// @param data  Pointer to the payload bytes.
    /// @param size  Payload length in bytes.
    /// @return False if the send fails.
    bool send(const void* data, uint32_t size);

    bool sendInputSnapshot(const InputSnapshot& snap);

    /// @brief Send a PING packet to the server for RTT measurement.
    void sendPing();

    /// @brief Update bandwidth stats. Call once per frame with the frame delta time.
    void updateStats(float dt);

    void onLocalPlayerReady(LocalPlayerReadyFn fn) { localPlayerReadyFn = std::move(fn); }

    /// @brief Receive and process one pending message.
    /// @return True if a message was received, false if the queue is empty.
    bool poll(Registry& registry);

    /// @brief Access current network statistics.
    const NetworkStats& getNetStats() const { return stats; }

private:
    MessageStream msgStream{nullptr};              ///< Framed message stream for server communication.
    NET_Address* serverAddr = nullptr;             ///< Resolved server address.
    std::optional<registry_serialization::Loader> registryLoader;
    LocalPlayerReadyFn localPlayerReadyFn;         ///< Called once the server assigns a player entity.
    std::optional<entt::entity> localPlayerEntity; ///< The local player's entity, once assigned by the server.
    bool localPlayerReadyNotified = false;         ///< True if localPlayerReadyFn has been called.

    NetworkStats stats;                            ///< Live network metrics.

    // Bandwidth tracking — accumulated between updateStats() calls.
    uint64_t bytesSentWindow = 0;
    uint64_t bytesRecvWindow = 0;
    uint32_t registryUpdatesWindow = 0;
    float statsAccumulator = 0.0f;
};
