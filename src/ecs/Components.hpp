#pragma once
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
    float yaw   = 0.0f; // radians, rotation around Y axis
    float pitch = 0.0f; // radians, clamped to ±89°
};

// ---------------------------------------------------------------------------
// Per-frame input snapshot (cleared each frame, populated by InputSystem)
// ---------------------------------------------------------------------------

struct InputState
{
    glm::vec2 moveDir{0.0f};  // WASD in [-1, 1], not yet rotated to world space
    glm::vec2 mouseDelta{0.0f};
    bool jumpPressed = false; // edge: first frame of press
    bool jumpHeld    = false; // level: held this frame
    bool crouchHeld  = false;
    bool glideHeld   = false; // same key as jump, used while airborne
};

// ---------------------------------------------------------------------------
// Player movement state machine
// ---------------------------------------------------------------------------

struct PlayerController
{
    // Ground contact
    bool onGround          = false;
    glm::vec3 groundNormal = {0.0f, 1.0f, 0.0f};

    // Wall contact (for wall jump)
    bool onWall          = false;
    glm::vec3 wallNormal = {0.0f, 0.0f, 0.0f};
    float wallCooldown   = 0.0f; // seconds before another walljump is allowed

    // Glide state
    bool gliding     = false;
    float glideTimer = 0.0f;

    // Coyote time (briefly after walking off a ledge, still allow jump)
    float coyoteTimer = 0.0f;

    // Player capsule geometry (used by physics & renderer)
    float halfWidth  = 32.0f; // Quake units, half-extent in X and Z
    float halfHeight = 36.0f; // half-extent in Y (total height = 72)
    float eyeHeight  = 28.0f; // above center (center + eyeHeight = eye pos)

    // Gravity override (direction + magnitude)
    glm::vec3 gravityDir = {0.0f, -1.0f, 0.0f};
    float gravityMag     = 800.0f; // Quake units/s²
};

// ---------------------------------------------------------------------------
// Gravity (per-entity override; defaults applied by PhysicsSystem)
// ---------------------------------------------------------------------------

struct GravityVolume
{
    glm::vec3 direction = {0.0f, -1.0f, 0.0f};
    float magnitude     = 800.0f;
};
