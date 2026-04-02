#pragma once
#include <cstdint>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// Network constants
// ---------------------------------------------------------------------------

constexpr uint16_t k_serverPort = 27015;
constexpr uint32_t k_magic = 0x54444F4D;                       // 'TDOM'
constexpr int k_maxPlayers = 4;
constexpr int k_serverTickHz = 64;                             // physics ticks per second
constexpr int k_snapshotHz = 20;                               // snapshots sent per second
constexpr int k_snapshotEvery = k_serverTickHz / k_snapshotHz; // = 3 ticks
constexpr int k_lagCompTicks = 96;                             // 1.5 s of history at 64 Hz
constexpr int k_maxPacketBytes = 1400;                         // stay under UDP MTU
constexpr float k_tickDt = 1.0f / k_serverTickHz;

// ---------------------------------------------------------------------------
// Packet type enum
// ---------------------------------------------------------------------------

enum class PacketType : uint8_t
{
    // Client → Server
    Connect = 0x01,    // join request + player name
    Disconnect = 0x02, // graceful leave
    Input = 0x03,      // per-tick input state

    // Server → Client
    ConnectAck = 0x10, // accepted: assigns clientId + current tick
    Snapshot = 0x11,   // full world state (player positions, health…)
    Event = 0x12,      // HitConfirm / Kill / Pickup etc.
    Kick = 0x13,       // rejected / server full
};

// ---------------------------------------------------------------------------
// Packet header  (10 bytes, present on every packet)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct PacketHeader
{
    uint32_t magic = k_magic;
    uint16_t sequence = 0;   // sender's outgoing sequence
    uint16_t ack = 0;        // last received sequence from the other side
    uint16_t ackBits = 0;    // bitfield: bits 0..15 = ack-1 .. ack-16
    uint8_t type = 0;        // PacketType
    uint8_t clientId = 0xFF; // 0xFF = server, 0-3 = client slot
};
static_assert(sizeof(PacketHeader) == 12, "header size changed");

// ---------------------------------------------------------------------------
// Button bitfield — packed into one byte
// ---------------------------------------------------------------------------

struct Buttons
{
    uint8_t raw = 0;
    bool jump() const { return (raw >> 0) & 1; }
    bool crouch() const { return (raw >> 1) & 1; }
    bool fire() const { return (raw >> 2) & 1; }
    bool altFire() const { return (raw >> 3) & 1; }
    bool knife() const { return (raw >> 4) & 1; }
    bool grapple() const { return (raw >> 5) & 1; }
    bool jumpHeld() const { return (raw >> 6) & 1; }

    static Buttons build(bool jmp, bool jmpHeld, bool cr, bool fi, bool af, bool kn, bool gr)
    {
        Buttons b;
        b.raw = static_cast<uint8_t>((uint8_t(jmp) << 0) | (uint8_t(jmpHeld) << 6) | (uint8_t(cr) << 1) |
                                     (uint8_t(fi) << 2) | (uint8_t(af) << 3) | (uint8_t(kn) << 4) | (uint8_t(gr) << 5));
        return b;
    }
};

// ---------------------------------------------------------------------------
// C2S: Connect
// ---------------------------------------------------------------------------

struct PktConnect
{
    PacketHeader hdr;
    char name[16]; // null-terminated, max 15 chars
};

// ---------------------------------------------------------------------------
// S2C: ConnectAck
// ---------------------------------------------------------------------------

struct PktConnectAck
{
    PacketHeader hdr;
    uint8_t clientId;    // assigned slot 0-3
    uint32_t serverTick; // current server tick (for clock sync)
};

// ---------------------------------------------------------------------------
// C2S: Input  (sent every client frame, ~60-144 Hz)
// ---------------------------------------------------------------------------

struct PktInput
{
    PacketHeader hdr;
    uint32_t clientTick; // client's tick estimate (for lag comp)
    float moveFwd;       // [-1, 1]
    float moveRight;     // [-1, 1]
    float yaw;           // radians
    float pitch;         // radians
    Buttons buttons;
    uint8_t _pad[3];
};
// sizeof(PktInput) = sizeof(PacketHeader) + 4+4+4+4+4+1+3 = 12+24 = 36

// ---------------------------------------------------------------------------
// Per-player state inside a snapshot
// ---------------------------------------------------------------------------

struct PlayerState
{
    uint8_t id;
    uint8_t flags;  // bit0=alive, bit1=onGround, bit2=onWall
    uint8_t health;
    uint8_t weapon; // WeaponId enum
    uint8_t ammo;
    uint8_t _pad[3];
    float posX, posY, posZ;
    float velX, velY, velZ;
    float yaw;
    float pitch;

    bool alive() const { return (flags >> 0) & 1; }
    bool onGround() const { return (flags >> 1) & 1; }
    bool onWall() const { return (flags >> 2) & 1; }

    static uint8_t makeFlags(bool alive, bool onGround, bool onWall)
    {
        return static_cast<uint8_t>((uint8_t(alive) << 0) | (uint8_t(onGround) << 1) | (uint8_t(onWall) << 2));
    }
};
// PlayerState: 5+3+12+12+8 = 40 bytes (packed)

// ---------------------------------------------------------------------------
// S2C: Snapshot  — one per snapshot tick (~20 Hz)
// ---------------------------------------------------------------------------

struct PktSnapshot
{
    PacketHeader hdr;
    uint32_t serverTick;
    uint8_t playerCount;
    uint8_t _pad[3];
    PlayerState players[k_maxPlayers]; // only first playerCount are valid
};

// ---------------------------------------------------------------------------
// S2C: Event types
// ---------------------------------------------------------------------------

enum class EventType : uint8_t
{
    HitConfirm = 0x01, // you hit someone
    Killed = 0x02,     // you killed someone
    Damaged = 0x03,    // you took damage
    Respawn = 0x04,    // you respawned
    PickedUp = 0x05,   // item pickup
};

struct PktEvent
{
    PacketHeader hdr;
    uint8_t eventType; // EventType
    uint8_t param1;    // target clientId / item type
    uint8_t param2;    // damage amount
    uint8_t _pad;
};

#pragma pack(pop)
