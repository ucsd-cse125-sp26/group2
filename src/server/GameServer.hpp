#pragma once
#include "../ecs/Components.hpp"
#include "../game/Physics.hpp"
#include "../game/Weapons.hpp"
#include "../game/World.hpp"
#include "../net/NetChannel.hpp"
#include "../net/Protocol.hpp"
#include "LagComp.hpp"

#include <array>
#include <cstdint>
#include <entt/entt.hpp>

// ---------------------------------------------------------------------------
// Per-client state on the server
// ---------------------------------------------------------------------------

struct ClientSlot
{
    bool connected = false;
    sockaddr_in addr = {};
    NetChannel chan;
    char name[16] = "Player";
    uint8_t id = 0xFF;

    entt::entity entity = entt::null; // ECS entity representing this player
    uint32_t lastInputTick = 0;       // most recent processed input tick
    float lastInputTime = 0.0f;

    // Server-side edge detection for held buttons.
    // Client sends fireHeld; server computes firePressed = fireHeld && !prevFireHeld.
    bool prevFireHeld = false;
    bool prevAltFireHeld = false;

    // Pending inputs not yet processed (in case of minor re-ordering)
    static constexpr int k_inputBuf = 8;
    PktInput inputBuf[k_inputBuf];
    int inputCount = 0;

    // Respawn timer (-1 = alive)
    float respawnTimer = -1.0f;
    int kills = 0;
    int deaths = 0;
};

// ---------------------------------------------------------------------------
// GameServer — authoritative game simulation
// ---------------------------------------------------------------------------

class GameServer
{
public:
    // Returns false on fatal error.
    bool init(uint16_t port = k_serverPort);
    void shutdown();

    // Main loop: call this in a tight loop; it sleeps internally.
    void run();

    // Single tick update (exposed for testing).
    void tick(float dt);

private:
    // ---- Network ----
    UdpSocket sock;
    uint8_t buf[k_maxPacketBytes];

    std::array<ClientSlot, k_maxPlayers> clients;

    uint8_t connectedCount = 0;

    // ---- Simulation ----
    World world;
    PhysicsConfig physicsCfg;
    entt::registry registry;
    LagComp lagComp;

    uint32_t serverTick = 0;
    double simTime = 0.0;

    // ---- Internal ----
    void receiveAll();
    void handlePacket(const PacketHeader* hdr, int len, const sockaddr_in& from);
    void handleConnect(const PktConnect* pkt, const sockaddr_in& from);
    void handleInput(ClientSlot& cl, const PktInput* pkt);
    void handleDisconnect(ClientSlot& cl);

    void processTick(float dt);
    void processWeapon(ClientSlot& cl,
                       const PktInput& inp,
                       bool fireHeld,
                       bool firePressed,
                       bool altFireHeld,
                       bool altFirePressed,
                       bool knifePressed,
                       float dt);
    void broadcastSnapshot();
    void pushLagCompSnapshot();
    void spawnPlayer(ClientSlot& cl);
    void killPlayer(ClientSlot& cl, int attackerId);
    void checkRespawns(float dt);

    void sendEvent(ClientSlot& cl, EventType evt, uint8_t p1 = 0, uint8_t p2 = 0);
    void sendEventAll(EventType evt, uint8_t p1 = 0, uint8_t p2 = 0);

    ClientSlot* findClient(const sockaddr_in& from);
    uint8_t nextFreeSlot() const;

    // Respawn positions — open floor areas, verified clear of all map geometry.
    // Floor top is y=0; player center at y=36 means feet exactly on floor.
    // Player 1 was wrongly at (400,36,0) which is inside the low platform (Y:[0,128]).
    static constexpr glm::vec3 k_spawns[k_maxPlayers] = {
        {0.0f, 36.0f, 0.0f},                      // dead center
        {-700.0f, 36.0f, -700.0f},                // SW corner (clear of tower & cover boxes)
        {700.0f, 36.0f, -700.0f},                 // SE corner (clear of platform & cover box)
        {700.0f, 36.0f, 700.0f},                  // NE corner (clear of bhop walls)
    };
    static constexpr float k_respawnDelay = 4.0f; // seconds
};
