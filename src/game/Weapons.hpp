#pragma once
#include "../ecs/Components.hpp"
#include "../game/World.hpp"

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// Per-weapon static stats
// ---------------------------------------------------------------------------

struct WeaponStats
{
    const char* name;
    int damage;       // HP per hit
    float fireRate;   // shots per second
    float reloadTime; // seconds
    int magSize;
    int reserveSize;
    float range;    // qu (0 = melee)
    float spread;   // cone half-angle radians (0 = pinpoint)
    bool automatic; // hold fire = full auto
};

constexpr WeaponStats k_weaponStats[static_cast<int>(WeaponId::Count)] = {
    // Fists
    {.name = "Fists",
     .damage = 30,
     .fireRate = 2.0f,
     .reloadTime = 0.0f,
     .magSize = 0,
     .reserveSize = 0,
     .range = 60.0f,
     .spread = 0.0f,
     .automatic = false},
    // Knife
    {.name = "Knife",
     .damage = 80,
     .fireRate = 1.5f,
     .reloadTime = 0.0f,
     .magSize = 0,
     .reserveSize = 0,
     .range = 72.0f,
     .spread = 0.0f,
     .automatic = false},
    // Pistol
    {.name = "Pistol",
     .damage = 34,
     .fireRate = 4.0f,
     .reloadTime = 1.2f,
     .magSize = 12,
     .reserveSize = 60,
     .range = 6400.0f,
     .spread = 0.02f,
     .automatic = false},
    // Rifle
    {.name = "Rifle",
     .damage = 26,
     .fireRate = 10.0f,
     .reloadTime = 2.5f,
     .magSize = 30,
     .reserveSize = 90,
     .range = 16000.0f,
     .spread = 0.007f,
     .automatic = true},
};

// ---------------------------------------------------------------------------
// Hitscan result
// ---------------------------------------------------------------------------

struct HitResult
{
    bool hit = false;
    glm::vec3 point = {0.0f, 0.0f, 0.0f};  // world position of impact
    glm::vec3 normal = {0.0f, 0.0f, 0.0f}; // surface normal at impact
    float dist = 0.0f;                     // distance from origin
    int victimId = -1;                     // player slot (-1 = geometry)
};

// ---------------------------------------------------------------------------
// Ray vs AABB (world geometry)
// ---------------------------------------------------------------------------

// Returns true if ray [origin, origin + dir*maxDist] hits the AABB.
// tHit is the parametric hit distance along dir.
bool rayVsAabb(const glm::vec3& origin,
               const glm::vec3& dir,
               float maxDist,
               const glm::vec3& boxMin,
               const glm::vec3& boxMax,
               float& tHit);

// Ray vs player capsule (approximated as AABB [pos-halfExt, pos+halfExt]).
bool rayVsCapsule(const glm::vec3& origin,
                  const glm::vec3& dir,
                  float maxDist,
                  const glm::vec3& capCenter,
                  float radius,
                  float halfHeight,
                  float& tHit);

// ---------------------------------------------------------------------------
// Weapons system — weapon tick, shoot, reload
// ---------------------------------------------------------------------------

// Called every server tick per player.
void weaponUpdate(WeaponState& ws, float dt);

// Attempt to fire; returns true if a shot was fired (updates ws.ammo / cooldown).
// Does NOT perform hit detection — call doHitscan separately.
bool weaponTryFire(WeaponState& ws, bool triggerHeld, bool triggerPressed, float dt);

// Attempt to reload; returns true if reload was initiated.
bool weaponTryReload(WeaponState& ws);

// ---------------------------------------------------------------------------
// Hitscan — cast a ray, check world geometry then player capsules
// ---------------------------------------------------------------------------

struct CapsuleInfo
{
    int id;           // player slot
    glm::vec3 center; // world position (feet center + halfHeight)
    float radius;     // qu
    float halfHeight; // qu
};

// Fire a hitscan ray. otherPlayers is the list of capsules to test against
// (EXCLUDING the shooter). Geometry is tested via world AABB boxes.
HitResult hitscanFire(const glm::vec3& eyePos,
                      const glm::vec3& aimDir,
                      const WeaponStats& stats,
                      const World& world,
                      const CapsuleInfo* others,
                      int otherCount);

// Melee attack: sphere-cast in front of the player.
HitResult meleeAttack(const glm::vec3& eyePos,
                      const glm::vec3& aimDir,
                      const WeaponStats& stats,
                      const CapsuleInfo* others,
                      int otherCount);
