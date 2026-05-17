/// @file CollisionSystem.cpp
/// @brief Implementation of swept-AABB collision detection and response.

#include "ecs/systems/CollisionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/GrenadeConfig.hpp"
#include "ecs/components/PlayerSimState.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/physics/DebugCollisionDraw.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhaseDiagnostic.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/physics/TriMeshCollision.hpp"
#include "ecs/systems/ExplosionSystem.hpp"
#include "ecs/systems/FireSystem.hpp"

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
        physics::debug::pushDepenContact(
            contactPoint, pushDir, minPen, physics::debug::ContactSource::BoxDepen, 0);
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
/// Delegates to `physics::depenetrateAABBvsTriMesh`, which uses per-triangle
/// SAT MTV — accurate enough that curved surfaces (cylinders, spheres) feel
/// curved instead of cubical.  Trade-off: at sharp triangle edges where
/// adjacent normals fight, per-triangle pushes can briefly disagree.  In
/// practice the pushback bias keeps the entity off the surface and the swept
/// collision (which still does precise per-triangle hits) is the primary path
/// for normal motion; this depenetration is only a safety net.
static void
depenetrateTriMesh(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldTriMesh& mesh)
{
    physics::depenetrateAABBvsTriMesh(pos, vel, halfExtents, mesh, k_pushback);
}

/// @brief Run all depenetration passes (planes, boxes, brushes, cylinders, spheres).
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

    for (const physics::WorldTriMesh& tm : world.triMeshes)
        depenetrateTriMesh(pos, vel, halfExtents, tm);
}

// Modern two-capsule character controller (Havok / Jolt / Unity-KCC pattern)
//
// Replaces:
//   * the legacy AABB lift→horiz→drop stair-step dance (`tryStepUp`),
//     which broke once the player became a true capsule because the
//     drop sweep's bounded-reach check rejects edge-corner contacts
//     that an AABB happened to reach,
//   * the per-feature MTV-summing capsule depen, whose front+back
//     contacts on thin geometry cancelled to zero push, and
//   * the CA-loop "contact branch", which pushed the player by a fixed
//     `k_pushback` per iteration in whatever direction the BVH happened
//     to return — causing the slow downward drift through thin floors.
//
// The new model:
//
//   * Per-pass-deepest-first depen (`depenetrateCapsuleVsWorld`) picks
//     the single deepest Voronoi contact across the whole world and
//     ejects fully; oscillation between opposing normals trips the
//     `emergencyUnstick` fallback (free-space probe in cardinal dirs).
//   * Horizontal motion uses the *walk capsule* (foot above stepHeight),
//     so stair risers / curbs / thresholds within stepHeight are
//     physically invisible to the sweep — they cannot be hit.
//   * Ground resolve (`resolveGround`) does ONE downward swept query for
//     the *full capsule* and snaps the foot onto whatever walkable
//     surface is below.  This is what climbs stairs (foot snaps to
//     tread), descends slopes (foot snaps to lower slope), and detects
//     "grounded" — all from a single query.
//   * Airborne vertical motion stays a swept full-capsule query, so
//     mid-air clipping into low walls / ceilings is honest.

/// @brief Build a `physics::CapsuleShape` query from a player's
/// `CollisionShape`.  Capsule axis is `+Y` when gravity is normal and
/// `-Y` when gravity is flipped — `probeGround` shoots its sweep along
/// `-capsule.up`, so flipping `up` flips the floor-search direction
/// without any other code change.  The capsule's Minkowski math is
/// symmetric under reflection of `up`, so swept queries on the flipped
/// capsule are numerically identical to the un-flipped queries.
static physics::CapsuleShape makeCapsuleQuery(const CollisionShape& shape, bool gravityFlipped)
{
    return physics::CapsuleShape{
        .radius = shape.radius,
        .halfHeight = shape.halfHeight,
        .up = glm::vec3{0.0f, gravityFlipped ? -1.0f : 1.0f, 0.0f},
    };
}

/// @brief Settle the player onto the closest walkable surface within
/// `maxSnapDistance` along the foot direction (`-capsule.up`).
///
/// Unified replacement for the legacy `Phase 2 slope-stick` +
/// `Phase 3 ground probe` + `tryStepUp drop sweep` — one swept query,
/// one snap, one source of truth.  Stair climbs naturally: the
/// preceding horizontal walk-capsule sweep slid past the riser; the
/// ground probe here uses the *full* capsule and finds the tread
/// below; the snap places the foot on the tread.
///
/// On success: sets `state.grounded = true`, captures the ground
/// normal, zeroes only the gravity-axis component of velocity
/// (preserves tangential slide), and adjusts `pos` along
/// `capsule.up` so the foot rests `k_pushback` above the surface.
///
/// Returns true when the player was settled on walkable ground.
static bool resolveGround(physics::CapsuleShape capsule,
                          glm::vec3& pos,
                          glm::vec3& vel,
                          PlayerVisState& state,
                          float maxSnapDistance,
                          const physics::WorldGeometry& world)
{
    const physics::GroundProbeResult probe = physics::probeGround(capsule, pos, maxSnapDistance, world);
    if (!probe.hit || !probe.walkable)
        return false;

    // Settle the capsule along the gravity axis so its surface touches the
    // ground point with `k_pushback` separation along the contact normal.
    // On a slope the contact normal is not parallel to `capsule.up`, so the
    // along-axis offset is `dot(normal, up) * (pushback + minkExt(normal))`
    // — using `halfHeight + radius` directly would float the player off
    // angled slopes by `(1 - dot(normal,up)) * minkExt` units.
    const float k_minkExtAlongNormal = capsule.minkowskiExtent(probe.normal);
    const float k_targetAlongUp =
        glm::dot(probe.point, capsule.up) + glm::dot(probe.normal, capsule.up) * (k_pushback + k_minkExtAlongNormal);
    const float k_currentAlongUp = glm::dot(pos, capsule.up);
    pos += capsule.up * (k_targetAlongUp - k_currentAlongUp);

    state.grounded = true;
    state.groundNormal = probe.normal;
    if (!state.grappleActive) {
        const float k_vAlongUp = glm::dot(vel, capsule.up);
        if (k_vAlongUp < 0.0f)
            vel -= capsule.up * k_vAlongUp;
    }
    return true;
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

    auto playerKernel = [&registry, dt, &world](entt::entity e) {
        auto& pos = registry.get<Position>(e);
        auto& vel = registry.get<Velocity>(e);
        const auto& shape = registry.get<CollisionShape>(e);
        auto& state = registry.get<PlayerVisState>(e);
        {
            // --- per-tick state setup ---
            const bool k_wasGrounded = state.grounded;
            state.grounded = false;

            const physics::CapsuleShape capsule = makeCapsuleQuery(shape, state.gravityFlipped);
            const glm::vec3 worldUp = capsule.up; // direction OPPOSITE gravity

            const bool diagOn = physics::diag::isEnabled();
            physics::diag::PlayerFrame diagFrame{};
            if (diagOn) {
                diagFrame.entity = e;
                diagFrame.posBefore = pos.value;
                diagFrame.velBefore = vel.value;
            }

            // --- Phase 0: depenetration (per-pass-deepest single-MTV resolver) ---
            //
            // Replaces the legacy per-feature MTV-summing depen.  Symmetric
            // contacts on thin geometry no longer cancel to zero push: we
            // pick the deepest single violator, eject fully, and re-probe.
            // Oscillation between opposing normals (genuinely ambiguous
            // two-sided geometry) trips the emergency-unstick fallback.
            physics::depenetrateCapsuleVsWorld(pos.value, vel.value, capsule, world);

            if (diagOn) {
                diagFrame.posAfterDepen = pos.value;
                diagFrame.depenPushDistance = glm::length(pos.value - diagFrame.posBefore);
                if (diagFrame.depenPushDistance > 20.0f)
                    diagFrame.flags |= physics::diag::PhaseFlag::DeepPenetration;
            }

            // --- Phase 1: motion integration ---
            //
            // Grounded ambulation: walk-capsule sweep over horizontal
            // velocity only.  The walk capsule's foot sits stepHeight above
            // the player's foot, so steps / risers within stepHeight are
            // physically invisible to the sweep — they cannot be hit.
            //
            // Airborne or grappling: full-capsule sweep over full 3D
            // velocity.  Feet must clear low walls in mid-air, and the
            // grapple cable should pull the player honestly through 3D
            // space.
            const bool useWalkCapsule = k_wasGrounded && !state.grappleActive;

            const physics::CapsuleShape sweepCapsule =
                useWalkCapsule ? capsule.walkShape(physics::k_stepHeight) : capsule;
            const glm::vec3 sweepCenterOffset =
                useWalkCapsule ? capsule.walkCenterOffset(physics::k_stepHeight) : glm::vec3{0.0f};

            glm::vec3 phaseVel = vel.value;
            if (useWalkCapsule) {
                // Strip the gravity-axis component; ground resolve below
                // settles the foot onto whatever's there.
                phaseVel -= worldUp * glm::dot(phaseVel, worldUp);
            }

            // Sub-step bound: cap per-substep motion at radius * safetyRatio
            // so high-velocity grapple yanks split into multiple substeps.
            const float fullStep = glm::length(phaseVel) * dt;
            const float maxSafeStep = sweepCapsule.radius * physics::k_substepSafetyRatio;
            const int numSubsteps =
                (physics::k_enableSubstepping && maxSafeStep > 1e-6f && fullStep > maxSafeStep)
                    ? std::min(physics::k_maxSubsteps,
                               static_cast<int>(std::ceil(fullStep / maxSafeStep)))
                    : 1;
            const float subDt = dt / static_cast<float>(numSubsteps);
            constexpr int k_maxCAIterations = 8;
            bool caExhaustedAnySubstep = false;

            for (int sub = 0; sub < numSubsteps; ++sub) {
                float remainingTime = subDt;
                int iter = 0;
                for (; iter < k_maxCAIterations && remainingTime > 1e-5f; ++iter) {
                    const glm::vec3 sweepStart = pos.value + sweepCenterOffset;
                    const float vLen = glm::length(phaseVel);
                    const float motionBound = vLen * remainingTime;
                    if (motionBound < 1e-6f) {
                        remainingTime = 0.0f;
                        break;
                    }

                    // Open-space fast-reject — omni-clearance lets us skip
                    // the sweep entirely when no geometry is within reach.
                    const physics::ClearanceResult clr =
                        physics::clearanceCapsuleVsWorld(sweepCapsule, sweepStart, world);
                    if (clr.distance > motionBound + k_pushback) {
                        pos.value += phaseVel * remainingTime;
                        remainingTime = 0.0f;
                        break;
                    }

                    // Sweep TOI for exact directional contact.
                    const glm::vec3 sweepEnd = sweepStart + phaseVel * remainingTime;
                    const physics::HitResult hit =
                        physics::sweepAll(sweepCapsule, sweepStart, sweepEnd, world);
                    if (!hit.hit) {
                        pos.value += phaseVel * remainingTime;
                        remainingTime = 0.0f;
                        break;
                    }

                    pos.value += phaseVel * hit.tFirst * remainingTime;
                    remainingTime *= (1.0f - hit.tFirst);

                    if (diagOn) {
                        ++diagFrame.bumpHits;
                        diagFrame.lastHitNormal = hit.normal;
                    }
                    if (physics::debug::isEnabled()) {
                        const float r = sweepCapsule.minkowskiExtent(hit.normal);
                        physics::debug::pushSweepContact(pos.value + sweepCenterOffset - hit.normal * r,
                                                         hit.normal,
                                                         physics::debug::ContactSource::PlaneSweep);
                    }

                    // Clip + pushback at the hit.  In the grounded /
                    // walk-capsule branch every hit is a wall (the walk
                    // capsule's foot cannot reach a floor), so we use the
                    // wall overbounce uniformly.  In the airborne / full-
                    // capsule branch we still treat floor hits as floors
                    // (settle grounded) for the same-tick landing case.
                    if (!useWalkCapsule) {
                        const bool landed = glm::dot(hit.normal, worldUp) >= physics::k_floorAngleCos;
                        if (landed) {
                            state.grounded = true;
                            state.groundNormal = hit.normal;
                            phaseVel = physics::clipVelocity(phaseVel, hit.normal, physics::k_overbounceFloor);
                        } else {
                            phaseVel = physics::clipVelocity(phaseVel, hit.normal, physics::k_overbounceWall);
                        }
                    } else {
                        phaseVel = physics::clipVelocity(phaseVel, hit.normal, physics::k_overbounceWall);
                    }
                    pos.value += hit.normal * k_pushback;
                }
                if (iter >= k_maxCAIterations && remainingTime > 1e-5f)
                    caExhaustedAnySubstep = true;
            }

            // Write back the (possibly clipped) velocity.
            if (useWalkCapsule) {
                vel.value.x = phaseVel.x;
                vel.value.z = phaseVel.z;
                // vel.y preserved — gravity component is handled by Phase 2.
            } else {
                vel.value = phaseVel;
            }

            // --- Phase 2: ground resolve ---
            //
            // One downward swept query with the full capsule.  When the
            // player was grounded last tick, the probe extends to
            // (effectiveStep + groundSnap) so it catches both step-ups
            // (after a walk-capsule sweep over a riser) and step-downs
            // (descending stairs / slopes).  When airborne, the probe is
            // just a tiny landing check.
            //
            // Skipped during grapple: the player flies freely along the
            // cable without being snapped to the ground.
            //
            // Skipped while actively jumping (vy strongly opposing gravity):
            // we don't want to swallow a fresh jump impulse.
            if (!state.grappleActive) {
                const float k_vAlongUp = glm::dot(vel.value, worldUp);
                const bool jumping = k_vAlongUp > 10.0f;
                if (!jumping) {
                    const float effStep = capsule.effectiveStepHeight(physics::k_stepHeight);
                    const float snap = k_wasGrounded
                                           ? (effStep + physics::k_groundSnapDistance)
                                           : physics::k_groundSnapDistance;
                    resolveGround(capsule, pos.value, vel.value, state, snap, world);
                }
            }

            // --- Phase 3: vertical motion when still airborne ---
            //
            // Grounded case is settled by Phase 2.  Airborne case (free-
            // fall, mid-jump, mid-grapple-arc) integrates the gravity-axis
            // velocity with a full-capsule swept query so feet honestly
            // clip into low walls / ceilings during vertical motion.
            //
            // (Horizontal was already integrated in Phase 1; this is just
            // the Y component.)
            if (!state.grounded && !state.grappleActive) {
                const float k_vAlongUp = glm::dot(vel.value, worldUp);
                const glm::vec3 vMotion = worldUp * k_vAlongUp;
                if (glm::dot(vMotion, vMotion) > 1e-12f) {
                    const glm::vec3 vTarget = pos.value + vMotion * dt;
                    const physics::HitResult vHit = physics::sweepAll(capsule, pos.value, vTarget, world);
                    if (!vHit.hit) {
                        pos.value = vTarget;
                    } else {
                        pos.value += vMotion * vHit.tFirst * dt;
                        pos.value += vHit.normal * k_pushback;
                        const bool landed = glm::dot(vHit.normal, worldUp) >= physics::k_floorAngleCos;
                        if (landed) {
                            state.grounded = true;
                            state.groundNormal = vHit.normal;
                            vel.value = physics::clipVelocity(vel.value, vHit.normal, physics::k_overbounceFloor);
                        } else {
                            vel.value = physics::clipVelocity(vel.value, vHit.normal, physics::k_overbounceWall);
                        }
                    }
                }
            }

            // --- Diag finalize ---
            if (diagOn) {
                diagFrame.posAfter = pos.value;
                diagFrame.velAfter = vel.value;
                if (caExhaustedAnySubstep)
                    diagFrame.flags |= physics::diag::PhaseFlag::BumpExhausted;
                if (state.grounded)
                    diagFrame.flags |= physics::diag::PhaseFlag::Grounded;
                if (state.grappleActive)
                    diagFrame.flags |= physics::diag::PhaseFlag::GrappleActive;
                if (state.gravityFlipped)
                    diagFrame.flags |= physics::diag::PhaseFlag::GravityFlipped;
                diagFrame.moveMode = static_cast<int>(state.moveMode);
                diagFrame.wallrunSide = static_cast<int>(state.wallRunSide);
                diagFrame.jumpCount = state.jumpCount;
                if (diagFrame.moveMode == 2) // WallRunning
                    diagFrame.flags |= physics::diag::PhaseFlag::WallRunning;
                if (diagFrame.moveMode == 1) // Sliding
                    diagFrame.flags |= physics::diag::PhaseFlag::Sliding;
                if (diagFrame.moveMode == 3) // Climbing
                    diagFrame.flags |= physics::diag::PhaseFlag::Climbing;
                if (diagFrame.moveMode == 4) // LedgeGrabbing
                    diagFrame.flags |= physics::diag::PhaseFlag::LedgeGrabbing;
                if (auto* sim = registry.try_get<PlayerSimState>(e); sim != nullptr) {
                    if (sim->jumpedThisTick)
                        diagFrame.flags |= physics::diag::PhaseFlag::DoubleJumped;
                }
                physics::diag::recordFrame(diagFrame);
            }
        }
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
