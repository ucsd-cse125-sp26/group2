/// @file KinematicCharacterController.cpp
/// @brief Capsule KCC implementation over static world geometry.

#include "ecs/systems/KinematicCharacterController.hpp"

#include "ecs/components/PlayerSimState.hpp"
#include "ecs/physics/DebugCollisionDraw.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhaseDiagnostic.hpp"
#include "ecs/physics/PhysicsConstants.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>

namespace systems
{

namespace
{

static constexpr float k_pushback = 0.03125f; // Quake DIST_EPSILON
static constexpr float k_minSafeDepenPush = 4.0f;

/// @brief Build a `physics::CapsuleShape` query from a player's
/// `CollisionShape`. Capsule axis is `+Y` when gravity is normal and `-Y`
/// when gravity is flipped.
physics::CapsuleShape makeCapsuleQuery(const CollisionShape& shape, bool gravityFlipped)
{
    return physics::CapsuleShape{
        .radius = shape.radius,
        .halfHeight = shape.halfHeight,
        .up = glm::vec3{0.0f, gravityFlipped ? -1.0f : 1.0f, 0.0f},
    };
}

bool finiteVec3(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool capsuleIsSafeAt(physics::CapsuleShape capsule, glm::vec3 pos, const physics::WorldGeometry& world)
{
    if (!finiteVec3(pos))
        return false;
    const physics::ClearanceResult clearance = physics::clearanceCapsuleVsWorld(capsule, pos, world);
    return clearance.distance >= -k_pushback;
}

glm::vec3 takePendingKccCorrection(PlayerSimState* simState)
{
    if (simState == nullptr)
        return glm::vec3{0.0f};

    const glm::vec3 correction = simState->pendingKccCorrection;
    simState->pendingKccCorrection = glm::vec3{0.0f};
    if (!finiteVec3(correction) || glm::length(correction) < 1e-5f)
        return glm::vec3{0.0f};

    return correction;
}

/// @brief Settle the player onto the closest walkable surface within
/// `maxSnapDistance` along the foot direction (`-capsule.up`).
bool resolveGround(physics::CapsuleShape capsule,
                   glm::vec3& pos,
                   glm::vec3& vel,
                   PlayerVisState& state,
                   float maxSnapDistance,
                   const physics::WorldGeometry& world)
{
    const physics::GroundProbeResult probe = physics::probeGround(capsule, pos, maxSnapDistance, world);
    if (!probe.hit || !probe.walkable)
        return false;

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

} // namespace

void runKinematicCharacterController(glm::vec3& pos,
                                     glm::vec3& vel,
                                     const CollisionShape& shape,
                                     PlayerVisState& state,
                                     float dt,
                                     const physics::WorldGeometry& world,
                                     entt::entity entity,
                                     bool jumpedThisTick,
                                     PlayerSimState* simState)
{
    const bool k_wasGrounded = state.grounded;
    state.grounded = false;

    const physics::CapsuleShape capsule = makeCapsuleQuery(shape, state.gravityFlipped);
    const glm::vec3 worldUp = capsule.up; // direction opposite gravity

    const bool diagOn = physics::diag::isEnabled();
    physics::diag::PlayerFrame diagFrame{};
    std::chrono::steady_clock::time_point diagStart{};
    if (diagOn) {
        diagStart = std::chrono::steady_clock::now();
        diagFrame.entity = entity;
        diagFrame.posBefore = pos;
        diagFrame.velBefore = vel;
    }

    const bool useWalkCapsule = k_wasGrounded && !state.grappleActive;
    const physics::CapsuleShape sweepCapsule = useWalkCapsule ? capsule.walkShape(physics::k_stepHeight) : capsule;
    const glm::vec3 sweepCenterOffset =
        useWalkCapsule ? capsule.walkCenterOffset(physics::k_stepHeight) : glm::vec3{0.0f};

    const glm::vec3 preDepenPos = pos;
    glm::vec3 pendingCorrection = takePendingKccCorrection(simState);
    glm::vec3 recoveryPos = pos + sweepCenterOffset;
    const glm::vec3 recoveryBeforeDepen = recoveryPos;
    glm::vec3 depenVel = vel;
    const float maxDepenPush =
        std::max(std::max(k_minSafeDepenPush, sweepCapsule.radius * 0.5f), glm::length(pendingCorrection) + k_pushback);
    const physics::DepenetrationResult depenResult = physics::depenetrateCapsuleVsWorldDetailed(
        recoveryPos,
        depenVel,
        sweepCapsule,
        world,
        physics::DepenetrationOptions{.maxPushDistance = maxDepenPush, .allowEmergencyUnstick = false});
    const bool hasPendingCorrection = glm::length(pendingCorrection) > 1e-5f;
    if ((depenResult.exceededMaxPush || depenResult.unresolvedOverlap || depenResult.emergencyUnstuck) &&
        simState != nullptr && simState->lastSafePositionValid &&
        capsuleIsSafeAt(capsule, simState->lastSafePosition, world))
    {
        pos = simState->lastSafePosition;
        recoveryPos = pos + sweepCenterOffset;
        vel = glm::vec3{0.0f};
        pendingCorrection = glm::vec3{0.0f};
    } else if (depenResult.exceededMaxPush || depenResult.unresolvedOverlap || depenResult.emergencyUnstuck) {
        pos = recoveryPos - sweepCenterOffset;
        if (!capsuleIsSafeAt(capsule, pos, world)) {
            pos = preDepenPos;
            vel = glm::vec3{0.0f};
        }
        recoveryPos = pos + sweepCenterOffset;
        pendingCorrection = glm::vec3{0.0f};
    } else if (!hasPendingCorrection) {
        vel = depenVel;
    } else {
        const glm::vec3 consumedByDepen = recoveryPos - recoveryBeforeDepen;
        const float requestedLen2 = glm::dot(pendingCorrection, pendingCorrection);
        if (requestedLen2 > 1e-8f) {
            const float consumedFraction =
                std::clamp(glm::dot(consumedByDepen, pendingCorrection) / requestedLen2, 0.0f, 1.0f);
            pendingCorrection *= (1.0f - consumedFraction);
        }
    }
    pos = recoveryPos - sweepCenterOffset;

    if (diagOn) {
        diagFrame.posAfterDepen = pos;
        diagFrame.depenPushDistance = glm::length(pos - diagFrame.posBefore);
        if (diagFrame.depenPushDistance > 20.0f)
            diagFrame.flags |= physics::diag::PhaseFlag::DeepPenetration;
    }

    glm::vec3 phaseVel = vel;
    if (useWalkCapsule)
        phaseVel -= worldUp * glm::dot(phaseVel, worldUp);

    glm::vec3 correctionVel = dt > 0.0f ? pendingCorrection / dt : glm::vec3{0.0f};
    glm::vec3 sweepVel = phaseVel + correctionVel;

    const float fullStep = glm::length(sweepVel) * dt;
    const float maxSafeStep = sweepCapsule.radius * physics::k_substepSafetyRatio;
    const int numSubsteps = (physics::k_enableSubstepping && maxSafeStep > 1e-6f && fullStep > maxSafeStep)
                                ? std::min(physics::k_maxSubsteps, static_cast<int>(std::ceil(fullStep / maxSafeStep)))
                                : 1;
    const float subDt = dt / static_cast<float>(numSubsteps);
    constexpr int k_maxCAIterations = 8;
    bool caExhaustedAnySubstep = false;
    int caIterations = 0;
    int clearanceQueries = 0;
    int sweepQueries = 0;
    int sweepHits = 0;

    for (int sub = 0; sub < numSubsteps; ++sub) {
        float remainingTime = subDt;
        int iter = 0;
        for (; iter < k_maxCAIterations && remainingTime > 1e-5f; ++iter) {
            ++caIterations;
            const glm::vec3 sweepStart = pos + sweepCenterOffset;
            sweepVel = phaseVel + correctionVel;
            const float vLen = glm::length(sweepVel);
            const float motionBound = vLen * remainingTime;
            if (motionBound < 1e-6f) {
                remainingTime = 0.0f;
                break;
            }

            ++clearanceQueries;
            const float clearanceSearchRadius = motionBound + sweepCapsule.radius + 16.0f;
            const physics::ClearanceResult clr =
                physics::clearanceCapsuleVsWorld(sweepCapsule, sweepStart, world, clearanceSearchRadius);
            if (clr.distance > motionBound + k_pushback) {
                pos += sweepVel * remainingTime;
                remainingTime = 0.0f;
                break;
            }

            const glm::vec3 sweepEnd = sweepStart + sweepVel * remainingTime;
            ++sweepQueries;
            const physics::HitResult hit = physics::sweepAll(sweepCapsule, sweepStart, sweepEnd, world);
            if (!hit.hit) {
                pos += sweepVel * remainingTime;
                remainingTime = 0.0f;
                break;
            }
            if (useWalkCapsule && glm::dot(hit.normal, worldUp) > 0.05f) {
                pos += sweepVel * remainingTime;
                remainingTime = 0.0f;
                break;
            }
            ++sweepHits;

            const float preHitRemainingTime = remainingTime;
            pos += sweepVel * hit.tFirst * preHitRemainingTime;
            remainingTime *= (1.0f - hit.tFirst);

            if (diagOn) {
                ++diagFrame.bumpHits;
                diagFrame.lastHitNormal = hit.normal;
            }
            if (physics::debug::isEnabled()) {
                const float r = sweepCapsule.minkowskiExtent(hit.normal);
                physics::debug::pushSweepContact(
                    pos + sweepCenterOffset - hit.normal * r, hit.normal, physics::debug::ContactSource::PlaneSweep);
            }

            bool clipGameplayVelocity = glm::dot(phaseVel, hit.normal) < 0.0f;
            glm::vec3 gameplayClipNormal = hit.normal;
            if (clipGameplayVelocity && glm::length(correctionVel) > 1e-5f) {
                clipGameplayVelocity = false;
                const float gameplayMotionBound = glm::length(phaseVel) * preHitRemainingTime;
                if (gameplayMotionBound > 1e-6f) {
                    ++sweepQueries;
                    const physics::HitResult gameplayHit =
                        physics::sweepAll(sweepCapsule, sweepStart, sweepStart + phaseVel * preHitRemainingTime, world);
                    if (gameplayHit.hit) {
                        const bool correctionMovesOutOfGameplayHit = glm::dot(correctionVel, gameplayHit.normal) > 0.0f;
                        if (gameplayHit.tFirst <= hit.tFirst + 1e-4f && glm::dot(phaseVel, gameplayHit.normal) < 0.0f &&
                            !correctionMovesOutOfGameplayHit)
                        {
                            clipGameplayVelocity = true;
                            gameplayClipNormal = gameplayHit.normal;
                        }
                    }
                }
            }

            if (!useWalkCapsule) {
                const bool landed = glm::dot(hit.normal, worldUp) >= physics::k_floorAngleCos;
                if (landed) {
                    state.grounded = true;
                    state.groundNormal = hit.normal;
                    if (clipGameplayVelocity)
                        phaseVel = physics::clipVelocity(phaseVel, gameplayClipNormal, physics::k_overbounceFloor);
                    if (glm::dot(correctionVel, hit.normal) < 0.0f)
                        correctionVel = physics::clipVelocity(correctionVel, hit.normal, physics::k_overbounceFloor);
                } else {
                    if (clipGameplayVelocity)
                        phaseVel = physics::clipVelocity(phaseVel, gameplayClipNormal, physics::k_overbounceWall);
                    if (glm::dot(correctionVel, hit.normal) < 0.0f)
                        correctionVel = physics::clipVelocity(correctionVel, hit.normal, physics::k_overbounceWall);
                }
            } else {
                if (clipGameplayVelocity)
                    phaseVel = physics::clipVelocity(phaseVel, gameplayClipNormal, physics::k_overbounceWall);
                if (glm::dot(correctionVel, hit.normal) < 0.0f)
                    correctionVel = physics::clipVelocity(correctionVel, hit.normal, physics::k_overbounceWall);
            }
            pos += hit.normal * k_pushback;
        }
        if (iter >= k_maxCAIterations && remainingTime > 1e-5f)
            caExhaustedAnySubstep = true;
    }

    if (useWalkCapsule) {
        vel.x = phaseVel.x;
        vel.z = phaseVel.z;
    } else {
        vel = phaseVel;
    }

    if (!state.grappleActive) {
        const float k_vAlongUp = glm::dot(vel, worldUp);
        const bool jumping = k_vAlongUp > 10.0f;
        if (!jumping) {
            const float effStep = capsule.effectiveStepHeight(physics::k_stepHeight);
            const float snap =
                k_wasGrounded ? (effStep + physics::k_groundSnapDistance) : physics::k_groundSnapDistance;
            resolveGround(capsule, pos, vel, state, snap, world);
        }
    }

    if (!state.grounded && !state.grappleActive) {
        const float k_vAlongUp = glm::dot(vel, worldUp);
        const glm::vec3 vMotion = worldUp * k_vAlongUp;
        if (glm::dot(vMotion, vMotion) > 1e-12f) {
            const glm::vec3 vTarget = pos + vMotion * dt;
            ++sweepQueries;
            const physics::HitResult vHit = physics::sweepAll(capsule, pos, vTarget, world);
            if (!vHit.hit) {
                pos = vTarget;
            } else {
                ++sweepHits;
                pos += vMotion * vHit.tFirst * dt;
                pos += vHit.normal * k_pushback;
                const bool landed = glm::dot(vHit.normal, worldUp) >= physics::k_floorAngleCos;
                if (landed) {
                    state.grounded = true;
                    state.groundNormal = vHit.normal;
                    vel = physics::clipVelocity(vel, vHit.normal, physics::k_overbounceFloor);
                } else {
                    vel = physics::clipVelocity(vel, vHit.normal, physics::k_overbounceWall);
                }
            }
        }
    }

    if (simState != nullptr && capsuleIsSafeAt(capsule, pos, world)) {
        simState->lastSafePosition = pos;
        simState->lastSafePositionValid = true;
    }

    if (diagOn) {
        const auto diagEnd = std::chrono::steady_clock::now();
        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(diagEnd - diagStart).count();
        const uint64_t clampedElapsedUs = elapsedUs > 0 ? static_cast<uint64_t>(elapsedUs) : 0u;
        diagFrame.posAfter = pos;
        diagFrame.velAfter = vel;
        const bool finiteState = std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) &&
                                 std::isfinite(vel.x) && std::isfinite(vel.y) && std::isfinite(vel.z) &&
                                 std::isfinite(state.groundNormal.x) && std::isfinite(state.groundNormal.y) &&
                                 std::isfinite(state.groundNormal.z);
        if (!finiteState)
            diagFrame.flags |= physics::diag::PhaseFlag::InvalidState;
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
        if (diagFrame.moveMode == 2)
            diagFrame.flags |= physics::diag::PhaseFlag::WallRunning;
        if (diagFrame.moveMode == 1)
            diagFrame.flags |= physics::diag::PhaseFlag::Sliding;
        if (jumpedThisTick)
            diagFrame.flags |= physics::diag::PhaseFlag::DoubleJumped;
        physics::diag::recordFrame(diagFrame);
        physics::diag::recordKccTimingFrame(physics::diag::KccTimingFrame{
            .entity = entity,
            .elapsedUs = clampedElapsedUs,
            .substeps = numSubsteps,
            .caIterations = caIterations,
            .clearanceQueries = clearanceQueries,
            .sweepQueries = sweepQueries,
            .sweepHits = sweepHits,
            .usedWalkCapsule = useWalkCapsule,
            .caExhausted = caExhaustedAnySubstep,
            .grounded = state.grounded,
            .moveMode = static_cast<int>(state.moveMode),
        });
    }
}

} // namespace systems
