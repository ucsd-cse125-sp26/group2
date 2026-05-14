/// @file Solver.cpp
/// @brief Sequential-Impulse (PGS) solver implementation.
///
/// Each iteration walks every contact point in stable (entityA, entityB,
/// pointIndex) order so the result is deterministic regardless of the
/// underlying hash-map insertion order.

#include "ecs/physics/Solver.hpp"

#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <vector>

namespace physics
{

namespace
{

struct BodyRef
{
    RigidBody* rb = nullptr;
    Velocity* vel = nullptr;
    AngularVelocity* angVel = nullptr;
    Position* pos = nullptr;
    bool isStatic = true;
};

[[nodiscard]] BodyRef gatherBody(Registry& registry, entt::entity e)
{
    BodyRef br;
    if (e == entt::null)
        return br;
    br.rb = registry.try_get<RigidBody>(e);
    br.vel = registry.try_get<Velocity>(e);
    br.angVel = registry.try_get<AngularVelocity>(e);
    br.pos = registry.try_get<Position>(e);
    br.isStatic = (br.rb == nullptr) || (br.rb->invMass <= 0.0f);
    return br;
}

/// @brief One contact point unpacked into solver-side scratch space.
/// Lifecycle: built once at the top of `solveContacts` from a manifold
/// reference, then iterated over `velocityIterations` times.
struct ContactRow
{
    ContactManifold* manifold = nullptr; // back-pointer for writing impulses
    int pointIndex = 0;

    glm::vec3 rA{0.0f}; // arm from body A's centre to contact (world)
    glm::vec3 rB{0.0f};

    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 tangent0{1.0f, 0.0f, 0.0f};
    glm::vec3 tangent1{0.0f, 0.0f, 1.0f};

    float effMassNormal = 0.0f;
    float effMassT0 = 0.0f;
    float effMassT1 = 0.0f;

    float bias = 0.0f; // Baumgarte + restitution

    float friction = 0.7f;
    float restitution = 0.0f;
};

/// @brief Compute orthonormal tangents perpendicular to `n`.
void makeTangents(glm::vec3 n, glm::vec3& t0, glm::vec3& t1)
{
    // Choose the axis most-perpendicular to n to start with.
    if (std::abs(n.x) >= 0.57735f)
        t0 = glm::normalize(glm::vec3{n.y, -n.x, 0.0f});
    else
        t0 = glm::normalize(glm::vec3{0.0f, n.z, -n.y});
    t1 = glm::cross(n, t0);
}

float effectiveMass(const BodyRef& a, const BodyRef& b, glm::vec3 axis, glm::vec3 rA, glm::vec3 rB)
{
    float invMass = 0.0f;
    if (!a.isStatic)
        invMass += a.rb->invMass;
    if (!b.isStatic)
        invMass += b.rb->invMass;

    if (!a.isStatic) {
        const glm::vec3 rAxA = glm::cross(rA, axis);
        invMass += glm::dot(rAxA, a.rb->invInertiaWorld * rAxA);
    }
    if (!b.isStatic) {
        const glm::vec3 rBxA = glm::cross(rB, axis);
        invMass += glm::dot(rBxA, b.rb->invInertiaWorld * rBxA);
    }
    return invMass > 0.0f ? 1.0f / invMass : 0.0f;
}

glm::vec3 relativeVelocity(const BodyRef& a, const BodyRef& b, glm::vec3 rA, glm::vec3 rB)
{
    glm::vec3 vA{0.0f};
    glm::vec3 vB{0.0f};
    if (a.vel != nullptr)
        vA = a.vel->value;
    if (b.vel != nullptr)
        vB = b.vel->value;
    if (a.angVel != nullptr)
        vA += glm::cross(a.angVel->value, rA);
    if (b.angVel != nullptr)
        vB += glm::cross(b.angVel->value, rB);
    return vB - vA;
}

void applyImpulse(BodyRef& a, BodyRef& b, glm::vec3 impulse, glm::vec3 rA, glm::vec3 rB)
{
    if (!a.isStatic) {
        a.vel->value -= impulse * a.rb->invMass;
        if (a.angVel != nullptr)
            a.angVel->value -= a.rb->invInertiaWorld * glm::cross(rA, impulse);
    }
    if (!b.isStatic) {
        b.vel->value += impulse * b.rb->invMass;
        if (b.angVel != nullptr)
            b.angVel->value += b.rb->invInertiaWorld * glm::cross(rB, impulse);
    }
}

} // namespace

void solveContacts(Registry& registry, ContactCache& cache, const SolverConfig& cfg, float dt)
{
    if (cache.size() == 0u || dt <= 0.0f)
        return;

    // 1. Gather every contact point into a flat ContactRow array.
    //    Manifolds are stored in a hash-map; build a stable iteration
    //    order by sorting on the canonical pair key so the solver is
    //    deterministic regardless of hash-map ordering.
    struct PairOrder
    {
        uint64_t key;
        ContactManifold* manifold;
    };
    std::vector<PairOrder> ordered;
    ordered.reserve(cache.size());
    for (auto& [k, entry] : cache) {
        ordered.push_back({k, &entry.manifold});
    }
    std::sort(ordered.begin(), ordered.end(), [](const PairOrder& a, const PairOrder& b) {
        return a.key < b.key;
    });

    std::vector<ContactRow> rows;
    rows.reserve(ordered.size() * 4);

    // 2. Build rows and warm-start.
    for (PairOrder& po : ordered) {
        ContactManifold& mf = *po.manifold;
        BodyRef ba = gatherBody(registry, mf.a);
        BodyRef bb = gatherBody(registry, mf.b);

        for (int p = 0; p < mf.pointCount; ++p) {
            const ContactPoint& cp = mf.points[p];
            ContactRow row;
            row.manifold = &mf;
            row.pointIndex = p;
            row.normal = mf.normal;
            makeTangents(row.normal, row.tangent0, row.tangent1);
            row.rA = cp.worldPositionA - (ba.pos != nullptr ? ba.pos->value : glm::vec3{0.0f});
            row.rB = cp.worldPositionB - (bb.pos != nullptr ? bb.pos->value : glm::vec3{0.0f});

            row.effMassNormal = effectiveMass(ba, bb, row.normal, row.rA, row.rB);
            row.effMassT0 = effectiveMass(ba, bb, row.tangent0, row.rA, row.rB);
            row.effMassT1 = effectiveMass(ba, bb, row.tangent1, row.rA, row.rB);

            // Baumgarte bias to push out residual penetration.
            const float pen = std::max(0.0f, cp.depth - cfg.linearSlop);
            row.bias = -(cfg.baumgarteScale / dt) *
                       std::min(pen, cfg.maxLinearCorrection);

            // Restitution from closing velocity at start of tick.
            const glm::vec3 vRel = relativeVelocity(ba, bb, row.rA, row.rB);
            const float vN = glm::dot(vRel, row.normal);
            row.restitution = cfg.defaultRestitution;
            if (-vN > cfg.velThreshForRestitution)
                row.bias += row.restitution * vN;

            row.friction = cfg.defaultFriction;

            // Warm-start: apply the cached impulses.
            const glm::vec3 P =
                row.normal * cp.normalImpulse + row.tangent0 * cp.tangentImpulse[0] + row.tangent1 * cp.tangentImpulse[1];
            applyImpulse(ba, bb, P, row.rA, row.rB);

            rows.push_back(row);
        }
    }

    // 3. Velocity iterations (PGS).
    for (int iter = 0; iter < cfg.velocityIterations; ++iter) {
        for (ContactRow& row : rows) {
            ContactManifold& mf = *row.manifold;
            ContactPoint& cp = mf.points[static_cast<size_t>(row.pointIndex)];

            BodyRef ba = gatherBody(registry, mf.a);
            BodyRef bb = gatherBody(registry, mf.b);

            // Normal impulse (with non-penetration constraint λN ≥ 0).
            {
                const glm::vec3 vRel = relativeVelocity(ba, bb, row.rA, row.rB);
                const float vN = glm::dot(vRel, row.normal);
                const float lambda = -(vN + row.bias) * row.effMassNormal;
                const float oldImpulse = cp.normalImpulse;
                cp.normalImpulse = std::max(oldImpulse + lambda, 0.0f);
                const float dLambda = cp.normalImpulse - oldImpulse;
                applyImpulse(ba, bb, row.normal * dLambda, row.rA, row.rB);
            }

            // Tangent impulses (Coulomb-clamped).
            const float maxFriction = row.friction * cp.normalImpulse;
            for (int t = 0; t < 2; ++t) {
                const glm::vec3 tDir = (t == 0) ? row.tangent0 : row.tangent1;
                const float effMass = (t == 0) ? row.effMassT0 : row.effMassT1;
                const glm::vec3 vRel = relativeVelocity(ba, bb, row.rA, row.rB);
                const float vT = glm::dot(vRel, tDir);
                const float lambda = -vT * effMass;
                const float oldImpulse = cp.tangentImpulse[t];
                const float clamped = std::clamp(oldImpulse + lambda, -maxFriction, maxFriction);
                const float dLambda = clamped - oldImpulse;
                cp.tangentImpulse[t] = clamped;
                applyImpulse(ba, bb, tDir * dLambda, row.rA, row.rB);
            }
        }
    }

    // 4. NGS-style position correction (optional, fixes residual penetration
    //    without modifying velocities — splits the position fix from the
    //    impulse fix to avoid jitter at large penetrations).  Each pass
    //    pushes bodies apart by `min(depth - slop, maxCorrection)`.
    for (int iter = 0; iter < cfg.positionIterations; ++iter) {
        for (PairOrder& po : ordered) {
            ContactManifold& mf = *po.manifold;
            BodyRef ba = gatherBody(registry, mf.a);
            BodyRef bb = gatherBody(registry, mf.b);
            const float invMassSum =
                (ba.isStatic ? 0.0f : ba.rb->invMass) + (bb.isStatic ? 0.0f : bb.rb->invMass);
            if (invMassSum <= 0.0f)
                continue;

            for (int p = 0; p < mf.pointCount; ++p) {
                const ContactPoint& cp = mf.points[static_cast<size_t>(p)];
                const float pen = std::max(0.0f, cp.depth - cfg.linearSlop);
                if (pen <= 0.0f)
                    continue;
                const float corr =
                    std::min(cfg.baumgarteScale * pen, cfg.maxLinearCorrection) / invMassSum;
                const glm::vec3 push = mf.normal * corr;
                if (!ba.isStatic && ba.pos != nullptr)
                    ba.pos->value -= push * ba.rb->invMass;
                if (!bb.isStatic && bb.pos != nullptr)
                    bb.pos->value += push * bb.rb->invMass;
            }
        }
    }
}

} // namespace physics
