#include "Weapons.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>

// ---------------------------------------------------------------------------
// Ray vs AABB (slab method)
// ---------------------------------------------------------------------------

bool rayVsAabb(const glm::vec3& origin,
               const glm::vec3& dir,
               float maxDist,
               const glm::vec3& boxMin,
               const glm::vec3& boxMax,
               float& tHit)
{
    float tmin = 0.0f;
    float tmax = maxDist;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(dir[i]) < 1e-8f) {
            if (origin[i] < boxMin[i] || origin[i] > boxMax[i])
                return false;
        } else {
            float invD = 1.0f / dir[i];
            float t1   = (boxMin[i] - origin[i]) * invD;
            float t2   = (boxMax[i] - origin[i]) * invD;
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return false;
        }
    }
    tHit = tmin;
    return tHit <= maxDist;
}

// ---------------------------------------------------------------------------
// Ray vs capsule — approximated as an AABB for simplicity.
// For an FPS the AABB hit-box is standard and feels more fair.
// ---------------------------------------------------------------------------

bool rayVsCapsule(const glm::vec3& origin,
                  const glm::vec3& dir,
                  float maxDist,
                  const glm::vec3& capCenter,
                  float radius,
                  float halfHeight,
                  float& tHit)
{
    glm::vec3 bMin = capCenter - glm::vec3(radius, halfHeight + radius, radius);
    glm::vec3 bMax = capCenter + glm::vec3(radius, halfHeight + radius, radius);
    return rayVsAabb(origin, dir, maxDist, bMin, bMax, tHit);
}

// ---------------------------------------------------------------------------
// Weapon update (called every tick: advance cooldown, reload timer)
// ---------------------------------------------------------------------------

void weaponUpdate(WeaponState& ws, float dt)
{
    if (ws.cooldown > 0.0f)
        ws.cooldown -= dt;
    if (ws.cooldown < 0.0f)
        ws.cooldown = 0.0f;

    if (ws.reloading) {
        ws.reload -= dt;
        if (ws.reload <= 0.0f) {
            ws.reloading   = false;
            ws.reload      = 0.0f;
            int need       = ws.ammo < 0 ? 0 : 0; // silence unused
            const auto& st = k_weaponStats[static_cast<int>(ws.active)];
            need           = st.magSize - ws.ammo;
            int take       = std::min(need, ws.reserve);
            ws.ammo += take;
            ws.reserve -= take;
        }
    }
}

// ---------------------------------------------------------------------------
// Fire attempt
// ---------------------------------------------------------------------------

bool weaponTryFire(WeaponState& ws, bool triggerHeld, bool triggerPressed, float dt)
{
    (void)dt;
    const auto& st = k_weaponStats[static_cast<int>(ws.active)];

    bool trigger = st.automatic ? triggerHeld : triggerPressed;
    if (!trigger)
        return false;
    if (ws.reloading)
        return false;
    if (ws.cooldown > 0.0f)
        return false;
    if (st.magSize > 0 && ws.ammo <= 0) {
        weaponTryReload(ws);
        return false;
    }

    ws.cooldown = 1.0f / st.fireRate;
    if (st.magSize > 0)
        ws.ammo--;
    ws.firing = true;
    return true;
}

bool weaponTryReload(WeaponState& ws)
{
    const auto& st = k_weaponStats[static_cast<int>(ws.active)];
    if (st.magSize == 0)
        return false; // melee weapon
    if (ws.reloading)
        return false;
    if (ws.ammo >= st.magSize)
        return false;
    if (ws.reserve <= 0)
        return false;
    ws.reloading = true;
    ws.reload    = st.reloadTime;
    return true;
}

// ---------------------------------------------------------------------------
// Hitscan
// ---------------------------------------------------------------------------

HitResult hitscanFire(const glm::vec3& eyePos,
                      const glm::vec3& aimDir,
                      const WeaponStats& stats,
                      const World& world,
                      const CapsuleInfo* others,
                      int otherCount)
{
    HitResult best;
    best.dist = stats.range;

    // Test world geometry (AABB boxes)
    for (const auto& box : world.boxes) {
        float tHit     = 0.0f;
        glm::vec3 bMin = box.center - box.half;
        glm::vec3 bMax = box.center + box.half;
        if (rayVsAabb(eyePos, aimDir, best.dist, bMin, bMax, tHit)) {
            if (tHit < best.dist) {
                best.hit      = true;
                best.dist     = tHit;
                best.point    = eyePos + aimDir * tHit;
                best.victimId = -1;
                // Approximate normal: find closest face
                glm::vec3 p = best.point - box.center;
                glm::vec3 norm(0.0f);
                float maxV = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    float v = std::abs(p[i]) / box.half[i];
                    if (v > maxV) {
                        maxV    = v;
                        norm    = glm::vec3(0.0f);
                        norm[i] = p[i] < 0.0f ? -1.0f : 1.0f;
                    }
                }
                best.normal = norm;
            }
        }
    }

    // Test player capsules
    for (int i = 0; i < otherCount; ++i) {
        const auto& cap = others[i];
        float tHit      = 0.0f;
        if (rayVsCapsule(eyePos, aimDir, best.dist, cap.center, cap.radius, cap.halfHeight, tHit)) {
            if (tHit < best.dist) {
                best.hit      = true;
                best.dist     = tHit;
                best.point    = eyePos + aimDir * tHit;
                best.normal   = -aimDir;
                best.victimId = cap.id;
            }
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
// Melee
// ---------------------------------------------------------------------------

HitResult meleeAttack(const glm::vec3& eyePos,
                      const glm::vec3& aimDir,
                      const WeaponStats& stats,
                      const CapsuleInfo* others,
                      int otherCount)
{
    HitResult best;
    best.dist = stats.range;

    for (int i = 0; i < otherCount; ++i) {
        const auto& cap = others[i];
        float tHit      = 0.0f;
        if (rayVsCapsule(eyePos, aimDir, best.dist, cap.center, cap.radius, cap.halfHeight, tHit)) {
            if (tHit < best.dist) {
                best.hit      = true;
                best.dist     = tHit;
                best.point    = eyePos + aimDir * tHit;
                best.normal   = -aimDir;
                best.victimId = cap.id;
            }
        }
    }
    return best;
}
