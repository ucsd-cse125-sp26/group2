#pragma once
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// ---------------------------------------------------------------------------
// Spatial
// ---------------------------------------------------------------------------

struct Transform
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

struct Velocity
{
    glm::vec3 linear{0.0f};
};

// ---------------------------------------------------------------------------
// Player view
// ---------------------------------------------------------------------------

struct CameraAngles
{
    float yaw = 0.0f;   // radians, rotation around Y axis
    float pitch = 0.0f; // radians, clamped to ±89°
};

// ---------------------------------------------------------------------------
// Per-frame input snapshot (cleared each frame, populated by InputSystem)
// ---------------------------------------------------------------------------

struct InputState
{
    glm::vec2 moveDir{0.0f};     // WASD in [-1, 1], not yet rotated to world space
    glm::vec2 mouseDelta{0.0f};
    bool jumpPressed = false;    // edge: first frame of press
    bool jumpHeld = false;       // level: held this frame
    bool crouchHeld = false;
    bool glideHeld = false;      // same key as jump, used while airborne
    bool firePressed = false;    // LMB rising edge
    bool fireHeld = false;       // LMB held
    bool altFirePressed = false; // RMB rising edge
    bool altFireHeld = false;
    bool knifePressed = false;   // F key rising edge
    bool reloadPressed = false;  // R key rising edge
    bool grapplePressed = false; // E key rising edge
    bool grappleHeld = false;    // E key held
};

// ---------------------------------------------------------------------------
// Player movement state machine
// ---------------------------------------------------------------------------

struct PlayerController
{
    // Ground contact
    bool onGround = false;
    glm::vec3 groundNormal = {0.0f, 1.0f, 0.0f};

    // Wall contact (for wall jump)
    bool onWall = false;
    glm::vec3 wallNormal = {0.0f, 0.0f, 0.0f};
    float wallCooldown = 0.0f; // seconds before another walljump is allowed

    // Glide state
    bool gliding = false;
    float glideTimer = 0.0f;

    // Coyote time (briefly after walking off a ledge, still allow jump)
    float coyoteTimer = 0.0f;

    // Player capsule geometry (used by physics & renderer)
    float halfWidth = 32.0f;  // Quake units, half-extent in X and Z
    float halfHeight = 36.0f; // half-extent in Y (total height = 72)
    float eyeHeight = 28.0f;  // above center (center + eyeHeight = eye pos)

    // Gravity override (direction + magnitude)
    glm::vec3 gravityDir = {0.0f, -1.0f, 0.0f};
    float gravityMag = 800.0f; // Quake units/s²

    // Surf: sliding on a ramp too steep to stand on (0.3 < upDot < 0.7)
    bool onSurf = false;
    glm::vec3 surfNormal = {0.0f, 1.0f, 0.0f};
};

// ---------------------------------------------------------------------------
// Gravity (per-entity override; defaults applied by PhysicsSystem)
// ---------------------------------------------------------------------------

struct GravityVolume
{
    glm::vec3 direction = {0.0f, -1.0f, 0.0f};
    float magnitude = 800.0f;
};

// ---------------------------------------------------------------------------
// Wall-run state
// ---------------------------------------------------------------------------

struct WallRun
{
    bool active = false;
    glm::vec3 wallNormal = {0.0f, 0.0f, 0.0f};
    glm::vec3 wallTangent = {0.0f, 0.0f, 0.0f};  // direction along the wall
    float timer = 0.0f;                          // how long we've been wall-running
    float cooldown = 0.0f;                       // must let go before next wall run
    static constexpr float k_maxDuration = 2.5f; // seconds before falling off
    static constexpr float k_minSpeed = 200.0f;  // qu/s needed to start/maintain
};

// ---------------------------------------------------------------------------
// Grapple hook
// ---------------------------------------------------------------------------

struct GrappleHook
{
    enum class State : uint8_t
    {
        Idle,
        Flying,   // projectile in flight
        Attached, // hooked to a surface
    };
    State state = State::Idle;
    glm::vec3 tipPos = {0.0f, 0.0f, 0.0f};        // current tip position
    glm::vec3 tipVel = {0.0f, 0.0f, 0.0f};        // velocity when flying
    glm::vec3 anchorPoint = {0.0f, 0.0f, 0.0f};   // world-space anchor when attached
    float ropeLength = 0.0f;                      // length at attach moment
    static constexpr float k_flySpeed = 2400.0f;  // qu/s
    static constexpr float k_pullForce = 1800.0f; // qu/s²
    static constexpr float k_maxRange = 3200.0f;  // qu
};

// ---------------------------------------------------------------------------
// Health / damage
// ---------------------------------------------------------------------------

struct Health
{
    int current = 100;
    int max = 100;
    int armor = 0;

    bool alive() const { return current > 0; }
    // Returns actual damage taken after armor absorption.
    int applyDamage(int raw)
    {
        int absorbed = (armor * raw) / 200; // armor absorbs up to 50 %
        absorbed = (absorbed > armor) ? armor : absorbed;
        armor -= absorbed;
        int taken = raw - absorbed;
        current -= taken;
        if (current < 0)
            current = 0;
        return taken;
    }
};

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------

enum class WeaponId : uint8_t
{
    Fists = 0,
    Knife = 1,
    Pistol = 2,
    Rifle = 3,
    Count,
};

struct WeaponState
{
    WeaponId active = WeaponId::Pistol;
    int ammo = 30;
    int reserve = 90;
    float cooldown = 0.0f; // seconds until next shot allowed
    float reload = 0.0f;   // seconds remaining in reload
    bool firing = false;
    bool reloading = false;
};

// ---------------------------------------------------------------------------
// Network identity (present on remote player entities in the client registry)
// ---------------------------------------------------------------------------

struct NetPlayer
{
    uint8_t id = 0xFF;        // server-assigned slot 0-3
    char name[16] = "Player"; // display name

    // Interpolation: we keep two snapshots and lerp between them
    glm::vec3 snapPosA = {0.0f, 0.0f, 0.0f};
    glm::vec3 snapPosB = {0.0f, 0.0f, 0.0f};
    float snapYawA = 0.0f;
    float snapYawB = 0.0f;
    float lerpT = 1.0f;   // 0→A, 1→B; advances with time
    float lerpDt = 0.05f; // seconds between A and B snapshots (1 / snapshotHz)

    bool alive = true;
};
