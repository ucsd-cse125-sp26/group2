/// @file CollisionSystem.cpp
/// @brief Implementation of swept-AABB collision detection and response.

#include "ecs/systems/CollisionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/GrenadeConfig.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerSimState.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/physics/DebugCollisionDraw.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhaseDiagnostic.hpp"
#include "ecs/physics/PhysicsPerfStats.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/physics/TriMeshCollision.hpp"
#include "ecs/systems/ExplosionSystem.hpp"
#include "ecs/systems/FireSystem.hpp"
#include "ecs/systems/KinematicCharacterController.hpp"
#include "ecs/systems/MovementSystem.hpp"

#include <glm/geometric.hpp>

// PR-7 (server-perf): optional parallel-STL hooks for the player loop.
// Header-only on the server build path; client TUs compile this file
// without the perf module on the include path, so we fall back to
// sequential there.
#if __has_include("perf/Parallel.hpp")
#include "perf/Parallel.hpp"
#define GROUP2_COLLISION_HAS_PARALLEL 1
#else
#define GROUP2_COLLISION_HAS_PARALLEL 0
#endif

#include <vector>

namespace systems
{

namespace
{

/// @brief Detonate a grenade at its current position based on its GrenadeConfig.
///
/// For Explosion-kind: queues a damage+knockback explosion.
/// For FireField-kind: spawns a FireField entity that applies DoT damage over time.
void detonateGrenade(Registry& registry, const Projectile& projectile, glm::vec3 position)
{
    if (!isGrenadeType(projectile.type)) {
        return;
    }
    const GrenadeConfig& cfg = getGrenadeConfig(projectile.type);
    switch (cfg.detonation) {
    case GrenadeDetonationKind::Explosion:
        queueExplosion(registry,
                       position,
                       cfg.explosionRadius,
                       cfg.damage,
                       projectile.owner,
                       cfg.damageFalloffExp,
                       cfg.selfDamageMult,
                       cfg.maxKnockback,
                       cfg.knockbackFalloffExp);
        break;
    case GrenadeDetonationKind::FireField:
        spawnFireField(registry, position, cfg.fireRadius, cfg.fireDuration, cfg.fireDps, projectile.owner);
        break;
    }
}

} // namespace

static constexpr float k_pushback = 0.03125f; // Quake DIST_EPSILON

// Depenetration

/// @brief Push the entity out of any infinite planes it currently overlaps.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param planes       Infinite planes to test against.
static void
depenetratePlanes(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, std::span<const physics::Plane> planes)
{
    uint32_t planeIdx = 0;
    for (const physics::Plane& plane : planes) {
        const float k_r = std::abs(plane.normal.x) * halfExtents.x + std::abs(plane.normal.y) * halfExtents.y +
                          std::abs(plane.normal.z) * halfExtents.z;
        const float k_dist = glm::dot(plane.normal, pos) - plane.distance;

        if (k_dist < k_r) {
            const float k_overlap = k_r - k_dist;
            pos += plane.normal * (k_overlap + k_pushback);

            const float k_into = glm::dot(vel, plane.normal);
            if (k_into < 0.0f)
                vel -= plane.normal * k_into;

            // Diagnostic contact: report the point on the plane closest to the entity.
            if (physics::debug::isEnabled()) {
                physics::debug::pushDepenContact(pos - plane.normal * k_r,
                                                 plane.normal,
                                                 k_overlap,
                                                 physics::debug::ContactSource::PlaneDepen,
                                                 planeIdx);
            }
        }
        ++planeIdx;
    }
}

/// @brief Push the entity out of a static AABB it currently overlaps.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param box          Static axis-aligned bounding box to test against.
static void depenetrateBox(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldAABB& box)
{
    const glm::vec3 k_expMin = box.min - halfExtents;
    const glm::vec3 k_expMax = box.max + halfExtents;

    // Not overlapping?
    if (pos.x < k_expMin.x || pos.x > k_expMax.x || pos.y < k_expMin.y || pos.y > k_expMax.y || pos.z < k_expMin.z ||
        pos.z > k_expMax.z)
        return;

    // Find the axis of least penetration and push out.
    float minPen = 1e30f;
    glm::vec3 pushDir{0.0f};

    // clang-format off
    struct { float pen; glm::vec3 dir; } faces[] = {
        {pos.x - k_expMin.x, {-1, 0, 0}},
        {k_expMax.x - pos.x, { 1, 0, 0}},
        {pos.y - k_expMin.y, { 0,-1, 0}},
        {k_expMax.y - pos.y, { 0, 1, 0}},
        {pos.z - k_expMin.z, { 0, 0,-1}},
        {k_expMax.z - pos.z, { 0, 0, 1}},
    };
    // clang-format on

    for (const auto& f : faces) {
        if (f.pen < minPen) {
            minPen = f.pen;
            pushDir = f.dir;
        }
    }

    pos += pushDir * (minPen + k_pushback);
    const float k_into = glm::dot(vel, pushDir);
    if (k_into < 0.0f)
        vel -= pushDir * k_into;

    if (physics::debug::isEnabled()) {
        // Contact lies on the face we just pushed out of, at the AABB centre's projection.
        const glm::vec3 boxCentre = (box.min + box.max) * 0.5f;
        const glm::vec3 contactPoint = pos - pushDir * std::abs(glm::dot(halfExtents, glm::abs(pushDir)));
        (void)boxCentre;
        physics::debug::pushDepenContact(contactPoint, pushDir, minPen, physics::debug::ContactSource::BoxDepen, 0);
    }
}

/// @brief Push the entity out of a convex brush it currently overlaps.
/// Only fires if the entity is inside ALL planes simultaneously.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param brush        Convex brush to test against.
static void
depenetrateBrush(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldBrush& brush)
{
    float minOverlap = 1e30f;
    int minPlane = -1;

    for (int i = 0; i < brush.planeCount; ++i) {
        const auto& p = brush.planes[i];
        const float k_r = std::abs(p.normal.x) * halfExtents.x + std::abs(p.normal.y) * halfExtents.y +
                          std::abs(p.normal.z) * halfExtents.z;
        const float k_dist = glm::dot(p.normal, pos) - p.distance;

        if (k_dist >= k_r)
            return; // outside this plane → not inside brush

        const float k_overlap = k_r - k_dist;
        if (k_overlap < minOverlap) {
            minOverlap = k_overlap;
            minPlane = i;
        }
    }

    if (minPlane < 0)
        return;

    const auto& plane = brush.planes[minPlane];
    pos += plane.normal * (minOverlap + k_pushback);

    const float k_into = glm::dot(vel, plane.normal);
    if (k_into < 0.0f)
        vel -= plane.normal * k_into;

    if (physics::debug::isEnabled()) {
        const float k_r = std::abs(plane.normal.x) * halfExtents.x + std::abs(plane.normal.y) * halfExtents.y +
                          std::abs(plane.normal.z) * halfExtents.z;
        physics::debug::pushDepenContact(pos - plane.normal * k_r,
                                         plane.normal,
                                         minOverlap,
                                         physics::debug::ContactSource::BrushDepen,
                                         static_cast<uint32_t>(minPlane));
    }
}

/// @brief Push the entity out of a vertical cylinder it currently overlaps.
static void
depenetrateCylinder(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldCylinder& cyl)
{
    // Minkowski expansion
    const float k_effR = cyl.radius + std::max(halfExtents.x, halfExtents.z);
    const float k_yMin = cyl.base.y - halfExtents.y;
    const float k_yMax = cyl.base.y + cyl.height + halfExtents.y;

    // Check Y overlap
    if (pos.y < k_yMin || pos.y > k_yMax)
        return;

    // Check XZ overlap (circle test)
    const float k_dx = pos.x - cyl.base.x;
    const float k_dz = pos.z - cyl.base.z;
    const float k_distXZ = std::sqrt(k_dx * k_dx + k_dz * k_dz);

    if (k_distXZ >= k_effR)
        return;

    // Inside — find least-penetration axis
    const float k_yPenBottom = pos.y - k_yMin;
    const float k_yPenTop = k_yMax - pos.y;
    const float k_xzPen = k_effR - k_distXZ;
    const float k_yPen = std::min(k_yPenBottom, k_yPenTop);

    glm::vec3 pushDir;
    float pen;

    if (k_yPen < k_xzPen) {
        // Push out along Y (whichever cap is closer)
        pushDir = (k_yPenBottom < k_yPenTop) ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        pen = k_yPen;
    } else {
        // Push out radially in XZ
        if (k_distXZ > 1e-6f)
            pushDir = glm::vec3(k_dx / k_distXZ, 0.0f, k_dz / k_distXZ);
        else
            pushDir = glm::vec3(1.0f, 0.0f, 0.0f); // degenerate: on axis
        pen = k_xzPen;
    }

    pos += pushDir * (pen + k_pushback);
    const float k_into = glm::dot(vel, pushDir);
    if (k_into < 0.0f)
        vel -= pushDir * k_into;

    if (physics::debug::isEnabled()) {
        // Contact point: a half-extent step back into the cylinder surface along the push direction.
        const float r = std::abs(pushDir.x) * halfExtents.x + std::abs(pushDir.y) * halfExtents.y +
                        std::abs(pushDir.z) * halfExtents.z;
        physics::debug::pushDepenContact(
            pos - pushDir * r, pushDir, pen, physics::debug::ContactSource::CylinderDepen, 0);
    }
}

/// @brief Push the entity out of a world sphere it currently overlaps.
static void
depenetrateSphere(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldSphere& sph)
{
    // Conservative Minkowski expansion
    const float k_effR = sph.radius + std::max({halfExtents.x, halfExtents.y, halfExtents.z});

    const glm::vec3 k_diff = pos - sph.center;
    const float k_dist = glm::length(k_diff);

    if (k_dist >= k_effR)
        return;

    // Inside — push outward from sphere centre
    glm::vec3 pushDir;
    if (k_dist > 1e-6f)
        pushDir = k_diff / k_dist;
    else
        pushDir = glm::vec3(0.0f, 1.0f, 0.0f); // degenerate: at centre

    const float k_pen = k_effR - k_dist;
    pos += pushDir * (k_pen + k_pushback);

    const float k_into = glm::dot(vel, pushDir);
    if (k_into < 0.0f)
        vel -= pushDir * k_into;

    if (physics::debug::isEnabled()) {
        physics::debug::pushDepenContact(
            sph.center + pushDir * sph.radius, pushDir, k_pen, physics::debug::ContactSource::SphereDepen, 0);
    }
}

/// @brief Push the entity out of a triangle mesh it currently overlaps.
///
/// Delegates to `physics::depenetrateAABBvsTriMesh`, the legacy projectile
/// AABB safety-net path. Player movement uses capsule depenetration in
/// `physics::depenetrateCapsuleVsWorld`; this remains for small projectile
/// bodies that still use AABB sweeps.
static void
depenetrateTriMesh(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldTriMesh& mesh)
{
    physics::depenetrateAABBvsTriMesh(pos, vel, halfExtents, mesh, k_pushback);
}

static bool overlapsAabb(const physics::WorldAABB& a, const physics::WorldAABB& b)
{
    return a.max.x >= b.min.x && a.min.x <= b.max.x && a.max.y >= b.min.y && a.min.y <= b.max.y && a.max.z >= b.min.z &&
           a.min.z <= b.max.z;
}

/// @brief Run all depenetration passes for legacy AABB bodies.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param world        World collision geometry.
static void
depenetrate(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldGeometry& world)
{
    depenetratePlanes(pos, vel, halfExtents, world.planes);

    for (const physics::WorldAABB& box : world.boxes)
        depenetrateBox(pos, vel, halfExtents, box);

    for (const physics::WorldBrush& brush : world.brushes)
        depenetrateBrush(pos, vel, halfExtents, brush);

    for (const physics::WorldCylinder& cyl : world.cylinders)
        depenetrateCylinder(pos, vel, halfExtents, cyl);

    for (const physics::WorldSphere& sph : world.spheres)
        depenetrateSphere(pos, vel, halfExtents, sph);

    const physics::WorldAABB query{.min = pos - halfExtents, .max = pos + halfExtents};
    if (world.staticBroadphase != nullptr && !world.staticBroadphase->nodes.empty()) {
        physics::queryStaticWorldBroadphase(*world.staticBroadphase, query, [&](uint32_t meshIndex) {
            if (meshIndex < world.triMeshes.size())
                depenetrateTriMesh(pos, vel, halfExtents, world.triMeshes[meshIndex]);
            return true;
        });
        return;
    }

    for (const physics::WorldTriMesh& tm : world.triMeshes) {
        const physics::WorldAABB meshAabb{.min = tm.boundsMin, .max = tm.boundsMax};
        if (overlapsAabb(meshAabb, query))
            depenetrateTriMesh(pos, vel, halfExtents, tm);
    }
}

void runCollision(Registry& registry, float dt, const physics::WorldGeometry& world)
{
    // CollisionSystem only reads/writes the replicated half (grounded /
    // groundNormal / grappleActive), so it takes PlayerVisState directly.
    // The simulation-only timer fields live in PlayerSimState and aren't
    // touched here.
    //
    // PR-7 (server-perf): the per-player swept-collision loop is
    // embarrassingly parallel — each iteration reads only the
    // shared (read-only) `WorldGeometry` and mutates only its own
    // entity's Position/Velocity/PlayerVisState. With AI bots
    // spreading across the map, this scope dominates the tick at
    // 200+ N (was 6.29 ms p99 at N=200 sequential). Pre-collect
    // entities, then parallelFor.

    auto playerView = registry.view<Position, Velocity, CollisionShape, PlayerVisState>();
    static thread_local std::vector<entt::entity> playerWork;
    playerWork.clear();
    for (auto e : playerView)
        playerWork.push_back(e);
    physics::perf::add(&physics::perf::FrameStats::collisionCalls);
    physics::perf::add(&physics::perf::FrameStats::collisionPlayers, static_cast<std::uint32_t>(playerWork.size()));

    auto playerKernel = [&registry, dt, &world](entt::entity e) {
        auto& pos = registry.get<Position>(e);
        auto& vel = registry.get<Velocity>(e);
        const auto& shape = registry.get<CollisionShape>(e);
        auto& state = registry.get<PlayerVisState>(e);
        PlayerSimState* sim = registry.try_get<PlayerSimState>(e);
        const bool jumpedThisTick = sim != nullptr && sim->jumpedThisTick;
        const physics::KccFrameResult kcc =
            runKinematicCharacterController(pos.value, vel.value, shape, state, dt, world, e, jumpedThisTick, sim);
        physics::perf::add(&physics::perf::FrameStats::kccCalls);
        physics::perf::add(&physics::perf::FrameStats::kccBumpHits, static_cast<std::uint32_t>(kcc.bumpHits));
        physics::perf::add(&physics::perf::FrameStats::kccCaIterations, static_cast<std::uint32_t>(kcc.caIterations));
        physics::perf::add(&physics::perf::FrameStats::kccSweepHits, static_cast<std::uint32_t>(kcc.sweepHits));
        const InputSnapshot* input = registry.try_get<InputSnapshot>(e);
        if (sim != nullptr && input != nullptr)
            reconcileMovementAfterKcc(pos.value, vel.value, shape, state, *sim, *input, world, kcc, dt);
    };

#if GROUP2_COLLISION_HAS_PARALLEL
    ::group2::perf::parallelFor(playerWork.begin(), playerWork.end(), playerKernel);
#else
    for (entt::entity e : playerWork)
        playerKernel(e);
#endif

    // Projectile entities
    registry.view<Position, Velocity, CollisionShape, Projectile>().each(
        [dt, &world, &registry](
            entt::entity e, Position& pos, Velocity& vel, const CollisionShape& shape, Projectile& projectile) {
            ProjectileConfig projConfig = getProjectileConfig(projectile.type);
            if (projectile.currentLifeTime >= projConfig.maxLifeTime) {
                if (projectile.explosive && projConfig.explosionRadius > 0.0f) {
                    queueExplosion(registry,
                                   pos.value,
                                   projConfig.explosionRadius,
                                   projectile.damage,
                                   projectile.owner,
                                   projConfig.explosionFalloffExponent,
                                   projConfig.selfDamageMultiplier,
                                   projConfig.maxKnockback,
                                   projConfig.knockbackFalloffExponent);
                }
                if (registry.valid(e)) {
                    registry.destroy(e);
                }
                return;
            }

            // Grenade fuse tick. Negative fuseTimer means "no fuse, impact-detonate" — leave alone.
            // (Sticky grenades like Impulse spawn with fuseTimer=-1 and only arm in the stick handler below.)
            // Tick BEFORE movement integration so a cooked grenade detonates exactly at its current
            // position rather than after one more tick of zero/coast velocity.
            if (projectile.fuseTimer >= 0.0f) {
                projectile.fuseTimer -= dt;
                if (projectile.fuseTimer <= 0.0f) {
                    detonateGrenade(registry, projectile, pos.value);
                    if (registry.valid(e)) {
                        registry.destroy(e);
                    }
                    return;
                }
            }

            projectile.currentLifeTime += dt;

            // Apply gravity to grenade projectiles only. Rockets (and other non-grenade
            // projectiles) remain ballistic-straight so existing tuning (e.g. rocket
            // initialProjectileSpeed = 3000 u/s) is unchanged. Match the player's
            // gravity constant; tune later via GrenadeConfig if too floaty.
            if (isGrenadeType(projectile.type)) {
                vel.value.y -= physics::k_gravity * dt;
            }

            // Phase 0 — Depenetration
            depenetrate(pos.value, vel.value, shape.halfExtents, world);

            // Phase 1 — Bump loop (collision response + stair stepping)
            float remainingTime = dt;

            for (int clip = 0; clip < 4 && remainingTime > 1e-5f; ++clip) {
                const glm::vec3 k_target = pos.value + vel.value * remainingTime;
                const physics::HitResult k_hit = physics::sweepAll(shape.halfExtents, pos.value, k_target, world);

                if (!k_hit.hit) {
                    pos.value = k_target;
                    break;
                }

                pos.value += vel.value * k_hit.tFirst * remainingTime;
                remainingTime *= (1.0f - k_hit.tFirst);

                // Sticky grenades: freeze velocity and arm the fuse if it wasn't already running.
                // Consume `sticky` so subsequent hits don't keep snapping to zero.
                if (projectile.sticky) {
                    vel.value = glm::vec3{0.0f};
                    projectile.sticky = false;
                    if (projectile.fuseTimer < 0.0f) {
                        const GrenadeConfig& cfg = getGrenadeConfig(projectile.type);
                        projectile.fuseTimer = cfg.fuseTime;
                    }
                    break; // done moving this tick — let the fuse take over
                }

                // Bouncy grenades: reflect velocity, lose energy via restitution, keep going.
                if (projectile.bounceRestitution > 0.0f) {
                    const glm::vec3 k_n = k_hit.normal;
                    vel.value = (vel.value - 2.0f * glm::dot(vel.value, k_n) * k_n) * projectile.bounceRestitution;
                    continue; // continue the bump loop with reflected velocity
                }

                // Default impact: detonate if applicable, then destroy.
                //   - Rocket-style explosive (projectile.explosive=true): unchanged path.
                //   - Grenade impact-detonate (Molotov has fuseTimer<0, sticky=false, explosive=false):
                //     route to detonateGrenade so it can spawn a FireField.
                if (projectile.explosive && projConfig.explosionRadius > 0.0f) {
                    queueExplosion(registry,
                                   pos.value,
                                   projConfig.explosionRadius,
                                   projectile.damage,
                                   projectile.owner,
                                   projConfig.explosionFalloffExponent,
                                   projConfig.selfDamageMultiplier,
                                   projConfig.maxKnockback,
                                   projConfig.knockbackFalloffExponent);
                } else if (isGrenadeType(projectile.type)) {
                    detonateGrenade(registry, projectile, pos.value);
                }
                if (registry.valid(e)) {
                    registry.destroy(e);
                }
                break;
            }
        });
}

} // namespace systems
