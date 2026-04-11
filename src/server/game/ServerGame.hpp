/// @file ServerGame.hpp
/// @brief Top-level server game loop integrating ECS and networking.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "network/Server.hpp"

#include <SDL3/SDL.h>

#include <entt/entity/entity.hpp>
#include <unordered_map>

/// @brief Top-level server game loop.
///
/// Owns the ECS registry and the network Server. Each tick it drains
/// incoming messages, runs all ECS systems, and broadcasts state.
class ServerGame
{
public:
    /// @brief Bind to the given address and port, spawn test entities.
    /// @param addr       Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port       TCP port to listen on.
    /// @param tickRateHz Physics tick rate in Hz (default 128).
    /// @return True on success, false on network or initialisation failure.
    bool init(const char* addr, Uint16 port, int tickRateHz = 128);

    /// @brief Block and run the game loop until shutdown() is called.
    void run();

    /// @brief Signal the loop to stop and release all resources.
    void shutdown();

    /// @brief Create a new player entity and map it to the given client ID.
    /// @param clientId Network client identifier for the new player.
    void initNewPlayer(int clientId);

private:
    /// @brief Apply a single event to the ECS registry.
    /// @param event The event to process.
    void eventHandler(Event event);

    /// @brief Advance one physics tick.
    /// @param dt       Fixed delta time in seconds (1 / tickRateHz).
    /// @param nextTick Performance counter deadline for the current tick.
    void tick(float dt, Uint64 nextTick);

    Server server;                                        ///< Owns the TCP socket and network I/O.
    Registry registry;                                    ///< ECS entity/component store.
    std::unordered_map<int, entt::entity> clientEntities; ///< Maps client IDs to ECS entities.
    bool running = false;                                 ///< Loop continues while true.
    int tickRateHz = 128;                                 ///< Physics ticks per second.
    int tickCount = 0;                                    ///< Total ticks since start, used for periodic logging.
};
