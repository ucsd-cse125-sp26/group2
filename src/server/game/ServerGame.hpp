/// @file ServerGame.hpp
/// @brief Top-level server game loop integrating ECS and networking.

#pragma once

#include "client/animation/AnimationLibrary.hpp"
#include "client/animation/CharacterAnimator.hpp"
#include "client/animation/CharacterRig.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/physics/MapLoader.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/Server.hpp"
#include "systems/MatchController.hpp"

#include <SDL3/SDL.h>

#include <entt/entity/entity.hpp>
#include <memory>
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

private:
    /// @brief Apply a single event to the ECS registry.
    /// @param event The event to process.
    void eventHandler(Event event);

    /// @brief Advance one physics tick.
    /// @param dt       Fixed delta time in seconds (1 / tickRateHz).
    /// @param nextTick Performance counter deadline for the current tick.
    void tick(float dt, Uint64 nextTick);

    /// @brief Create a new player entity and map it to the given client ID.
    /// @param clientId Network client identifier for the new player.
    void initNewPlayerEntity(ClientId clientId);

    /// @brief Remove player entity from ECS.
    /// @param clientId Network client identifier for the player.
    void deletePlayerEntity(ClientId clientId);

    /// @brief Initialise the server-side animation subsystem (skeleton, clips, hitboxes).
    /// Called once during init() after map loading.
    void initAnimation();

    /// @brief Create and store a server-side animator for the given player entity.
    void attachServerAnimator(entt::entity player);

    /// @brief Remove the server-side animator for the given entity.
    void detachServerAnimator(entt::entity player);

    /// @brief Update all server-side animators and recompute hitbox capsules.
    /// Called once per tick before weapon/damage systems.
    void updateAnimationAndHitboxes(float dt);

    physics::MapCollisionData mapCollision_; ///< Map collision data — owns vectors backing activeWorld().

    Server server;                           ///< Owns the TCP socket and network I/O.
    Registry registry;                       ///< ECS entity/component store.
    MatchController matchController;         ///< Manages match flow and state.
    std::unordered_map<ClientId, entt::entity> clientEntities; ///< Maps client IDs to ECS entities.
    std::vector<NetKillEvent> pendingKillEvents; ///< Accumulates kill events waiting for network broadcast.
    bool running = false;                        ///< Loop continues while true.
    int tickRateHz = 128;                        ///< Physics ticks per second.
    int tickCount = 0;                           ///< Total ticks since start, used for periodic logging.

    // ── Server-side animation subsystem ──
    CharacterRig serverRig_;             ///< Shared skeleton (loaded from same FBX as client).
    AnimationLibrary serverAnimLibrary_; ///< Animation clips for server-side sampling.
    HitboxRig hitboxRig_;                ///< Shared hitbox capsule definitions.
    float rigScale_ = 1.0f;              ///< Rig model-space → game-unit scale factor.
    float rigMeshMinY_ = 0.0f;           ///< Minimum Y of bind-pose mesh (for vertical offset).
    bool animationLoaded_ = false;       ///< True if rig+clips loaded successfully.

    /// Per-entity server animators (not ECS components to avoid pulling animation
    /// headers into the component registry).
    std::unordered_map<entt::entity, std::unique_ptr<CharacterAnimator>> serverAnimators_;
};
