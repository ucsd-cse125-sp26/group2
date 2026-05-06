/// @file GamepadAimAssistSystem.hpp
/// @brief AAA-style controller aim assist — applied after runGamepadLook.
///
/// Two-stage controller assist that *helps the player track*, rather than
/// locking onto a body part:
///
///   1. **AABB-anchored target slowdown / aim friction** — when the crosshair
///      is on an enemy, we *refund* part of the player's just-applied stick
///      input, lowering effective sensitivity in the kill zone.
///
///   2. **Movement-tracking rotational assist** — the camera receives a
///      rotation toward where the *anchor* on the target moved between the
///      last frame and this one, scaled by `rotationalCompensation`
///      (default 0.6).  The anchor is a point on the target's AABB that
///      *follows where the player is aiming* — so dragging the stick up
///      moves the anchor up on the body, dragging right moves it right,
///      etc.  The player picks where on the body to track; aim assist just
///      contributes a fraction of the angular velocity needed to stay
///      glued to that point as the enemy and player move around.
///
/// **Why this is weaker than head/torso magnet pull:**
///   - A stationary enemy contributes ZERO rotational pull (no aimbot when
///     standing still — Δanchor_world = 0).
///   - The pull is gated by *change* in apparent position, scaled by a
///     fraction (so the player still has to aim).
///   - There's no head preference — the player's view chooses where on the
///     body to track.  Want to land headshots?  Aim a bit higher and the
///     anchor follows.
///
/// **Activation gate** — both effects are gated on the player actually moving
/// a stick (≥5% on either left or right stick by default).  Holding still
/// gives full manual control.  Standard CoD/Apex/Halo convention.
///
/// **Server-blind** — modifies only the local player's `InputSnapshot.yaw/
/// pitch`, so prediction & replication are unaffected.
///
/// **Mouse-only players** see nothing — the system early-outs when no gamepad
/// is connected.

#pragma once

#include "InputSampleSystem.hpp" // for the gamepad::normaliseAxis helper + samplers
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Controllable.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>
#include <limits>

namespace systems
{

/// @brief Tunable parameters for gamepad aim assist.
///
/// Exposed in the ECS inspector so testers can dial it in live.  Defaults
/// are tuned to feel like *assist*, not auto-aim — a moving enemy is
/// easier to track but a stationary one is unaffected.
struct GamepadAimAssistConfig
{
    bool enabled = true;

    /// @brief Inner cone (degrees from crosshair) where pull strength is at
    /// maximum.  Anywhere inside this cone, the full compensation applies.
    float innerConeDeg = 3.0f;

    /// @brief Outer cone (degrees from crosshair) where pull falls to zero.
    /// Pull lerps linearly between inner and outer cones.
    float outerConeDeg = 8.0f;

    /// @brief Maximum world distance to a target at which aim assist applies.
    float maxRange = 3000.0f;

    /// @brief Stick activation threshold (0..1 fraction of full deflection).
    /// Either the left (movement) OR the right (look) stick must exceed this
    /// for aim assist to fire.  Holding both still disables the effect.
    float activationStickThresh = 0.00f;

    /// @brief Tracking compensation factor.  When the *apparent angular
    /// position* of the anchor on the target changes by Δθ between frames
    /// (due to enemy motion + player translation), aim assist contributes
    /// `rotationalCompensation × Δθ` to the camera rotation.  The player
    /// still has to provide the remainder manually.
    ///
    /// 0.0 = no rotational help, 1.0 = perfect tracking (aimbot).
    /// Default 0.8 — assist contributes 80 % of the angular velocity
    /// needed to glue to a moving target, leaving 20 % for the player.
    /// Bumped up from 0.6 after tester feedback that 60 % felt weak.
    float rotationalCompensation = 0.8f;

    /// @brief Hard cap on rotational pull this frame, in radians/second.
    /// Prevents the assist from teleporting onto a target that just spawned
    /// or warped via server snapshot correction.  3.0 rad/s ≈ 172°/s is
    /// generous; legitimate tracking deltas are typically well below.
    float maxPullRate = 3.0f;

    /// @brief Slowdown factor (0..1).  When the crosshair is on a target
    /// the effective look sensitivity is multiplied by this much.  Lower
    /// = stronger slowdown / "stickier" feel inside the kill zone.
    /// Default 0.35 = look at 35 % speed on-target; aggressive enough that
    /// the crosshair perceptibly resists overshoot when flicking onto an
    /// enemy, while still letting the player swing past if they really
    /// want to.  Tuned tighter than the previous 0.6 after tester feedback
    /// that the slowdown felt subtle.
    float slowdownStrength = 0.1f;
};

/// @brief Per-frame state required to compute the rotational pull as a
/// *delta* between frames.  Owned by Game; passed by reference into
/// `runGamepadAimAssist`.  Never serialised — purely local presentation.
struct GamepadAimAssistState
{
    /// @brief Target locked-onto last frame (`entt::null` when none).  Used
    /// to detect target-switch and re-initialise the anchor.
    entt::entity lastTarget = entt::null;
    /// @brief Anchor offset from the target's `Position.value` in world axes.
    /// Updated each frame to where the player's camera ray intersects (or
    /// is closest to) the target's AABB.  Stays inside the AABB.
    glm::vec3 anchorLocal{0.0f};
    /// @brief Target's world position last frame — combined with the (then-)
    /// `anchorLocal` to recover where the anchor was in world space.
    glm::vec3 lastTargetPos{0.0f};
    /// @brief Local player's eye position last frame.  Including this in
    /// the angular-delta calculation lets the pull respond to the player's
    /// own translation too — exactly what the user asked for.
    glm::vec3 lastEye{0.0f};
    /// @brief False until we have *one* completed frame of history for the
    /// current target.  Skips the angular-delta step on acquisition (no
    /// previous frame to subtract).
    bool initialised = false;
};

namespace aimassist
{

/// @brief Convert a world-space direction into yaw/pitch matching the
/// renderer convention used by `cachedCamFwd_`:
///   fwd = (sin(yaw)·cos(pitch),  −sin(pitch),  cos(yaw)·cos(pitch))
inline void dirToYawPitch(const glm::vec3& dir, float& outYaw, float& outPitch)
{
    outYaw = std::atan2(dir.x, dir.z);
    outPitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));
}

/// @brief Linear cone falloff: 1.0 inside `innerRad`, 0.0 outside `outerRad`.
inline float coneFalloff(float angleRad, float innerRad, float outerRad)
{
    if (angleRad <= innerRad)
        return 1.0f;
    if (angleRad >= outerRad)
        return 0.0f;
    return (outerRad - angleRad) / (outerRad - innerRad);
}

/// @brief Find the world point where the camera ray hits the target's AABB,
/// or — if it misses — the closest point on the AABB to the ray's projection
/// at target distance.  Always returns a point inside or on the AABB.
inline glm::vec3 rayOntoAABB(
    const glm::vec3& eye, const glm::vec3& dir, const glm::vec3& aabbMin, const glm::vec3& aabbMax, float fallbackDist)
{
    // Slab method.  Reject divisions by ~0 by treating tiny `dir[i]` as
    // "ray parallel to axis"; if origin is outside the slab on that axis,
    // the ray can never hit (we'll fall through to the fallback below).
    float tEnter = -std::numeric_limits<float>::infinity();
    float tExit = std::numeric_limits<float>::infinity();
    bool hits = true;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dir[i]) < 1e-6f) {
            if (eye[i] < aabbMin[i] || eye[i] > aabbMax[i]) {
                hits = false;
                break;
            }
        } else {
            float t1 = (aabbMin[i] - eye[i]) / dir[i];
            float t2 = (aabbMax[i] - eye[i]) / dir[i];
            if (t1 > t2)
                std::swap(t1, t2);
            tEnter = std::max(tEnter, t1);
            tExit = std::min(tExit, t2);
            if (tEnter > tExit) {
                hits = false;
                break;
            }
        }
    }
    if (hits && tEnter >= 0.0f) {
        return eye + dir * tEnter;
    }
    // Fallback: clamp the ray-at-target-distance point onto the AABB.  Always
    // produces a sensible "closest face/edge/corner" anchor — used when the
    // player is aiming just past the silhouette of the target.
    const glm::vec3 raySample = eye + dir * fallbackDist;
    return glm::clamp(raySample, aabbMin, aabbMax);
}

} // namespace aimassist

/// @brief Apply gamepad aim assist (slowdown + movement-tracking pull) to
/// the local player's InputSnapshot.  Must run AFTER `runGamepadLook` so
/// we can refund part of the player input it just integrated.
///
/// @param registry  ECS registry.
/// @param gamepad   Open gamepad handle (nullptr → no-op).
/// @param cfg       Tuning parameters.
/// @param state     Persistent per-frame state (anchor + previous-frame snapshot).
/// @param lookSens  Same value passed to runGamepadLook (rad/s @ full deflection).
/// @param dt        Frame delta time (seconds).
inline void runGamepadAimAssist(Registry& registry,
                                SDL_Gamepad* gamepad,
                                const GamepadAimAssistConfig& cfg,
                                GamepadAimAssistState& state,
                                float lookSens,
                                float dt)
{
    if (!gamepad || !cfg.enabled) {
        // Drop our memory of any previous target so the next acquisition
        // re-initialises cleanly (otherwise a stale anchor could fire one
        // bogus pull when assist is re-enabled).
        state.lastTarget = entt::null;
        state.initialised = false;
        return;
    }

    // ── Activation gate ───────────────────────────────────────────────────
    const float lx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    const float ly = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    const float rx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    const float ry = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));

    const float moveStickMag = std::sqrt(lx * lx + ly * ly);
    const float lookStickMag = std::sqrt(rx * rx + ry * ry);

    // When activationStickThresh <= 0, aim assist is always active (useful
    // for testing rotational assist on a single PC without a second player).
    const bool alwaysActive = cfg.activationStickThresh <= 0.0f;
    if (!alwaysActive && moveStickMag < cfg.activationStickThresh && lookStickMag < cfg.activationStickThresh) {
        // Player held still — keep the state so the anchor doesn't reset
        // when they nudge again, but skip pull/slowdown this frame.
        return;
    }

    // ── Local player aim state ────────────────────────────────────────────
    entt::entity localEntity = entt::null;
    glm::vec3 eye{0.0f};
    float curYaw = 0.0f, curPitch = 0.0f;
    bool foundLocal = false;
    registry.view<LocalPlayer, Position, CollisionShape, InputSnapshot, PlayerVisState>().each(
        [&](entt::entity e,
            const Position& pos,
            const CollisionShape& shape,
            const InputSnapshot& snap,
            const PlayerVisState& pvis) {
            localEntity = e;
            const float eyeOffset = shape.halfExtents.y * 0.77f;
            const float aaEyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
            eye = pos.value + glm::vec3{0.0f, eyeOffset * aaEyeDir, 0.0f};
            curYaw = snap.yaw;
            curPitch = snap.pitch;
            foundLocal = true;
        });
    if (!foundLocal)
        return;

    // Current aim direction, in the renderer's convention.
    const float cosPitchInit = std::cos(curPitch);
    const glm::vec3 camFwd{std::sin(curYaw) * cosPitchInit, -std::sin(curPitch), std::cos(curYaw) * cosPitchInit};

    // ── Target selection ──────────────────────────────────────────────────
    // Pick the alive remote player whose Position is closest to the crosshair
    // in angular terms, range-gated and LOS-clear (no walls in the way).
    // We use Position (AABB centre) for selection; the *anchor on the AABB*
    // is computed below.  Selecting on AABB centre is stable — selecting on
    // the anchor itself would create feedback when the anchor moves.
    entt::entity bestTarget = entt::null;
    glm::vec3 bestTargetPos{0.0f};
    glm::vec3 bestHalfExtents{0.0f};
    float bestAngle = glm::radians(cfg.outerConeDeg);
    float bestDist = cfg.maxRange;

    const auto& world = physics::activeWorld();

    registry.view<Position, CollisionShape, PlayerVisState>(entt::exclude<LocalPlayer>)
        .each([&](entt::entity e, const Position& pos, const CollisionShape& shape, const PlayerVisState& pvis) {
            if (e == localEntity)
                return;
            if (pvis.isDead)
                return;
            if (registry.all_of<RespawnTimer>(e))
                return;

            const glm::vec3 toCentre = pos.value - eye;
            const float dist = glm::length(toCentre);
            if (dist < 1e-3f || dist > cfg.maxRange)
                return;

            const glm::vec3 dirToCentre = toCentre / dist;
            const float dot = glm::clamp(glm::dot(camFwd, dirToCentre), -1.0f, 1.0f);
            if (dot <= 0.0f)
                return; // behind camera

            const float angle = std::acos(dot);
            if (angle >= bestAngle)
                return;

            // LOS check against world geometry.  We don't test player
            // hitboxes — the target's own capsule would self-occlude.
            const physics::HitscanHit blocker = physics::raycastWorld(eye, dirToCentre, world);
            if (blocker.hit && blocker.distance < dist - 1.0f)
                return;

            bestAngle = angle;
            bestTarget = e;
            bestTargetPos = pos.value;
            bestHalfExtents = shape.halfExtents;
            bestDist = dist;
        });

    if (bestTarget == entt::null) {
        // Lost target — clear state so the next acquisition starts fresh.
        state.lastTarget = entt::null;
        state.initialised = false;
        return;
    }

    // Cone falloff: 1.0 inside inner cone, 0.0 outside outer cone.
    const float coneFactor =
        aimassist::coneFalloff(bestAngle, glm::radians(cfg.innerConeDeg), glm::radians(cfg.outerConeDeg));
    // Distance falloff: 1.0 at point-blank, 0.0 at maxRange.
    const float distFactor = 1.0f - std::clamp(bestDist / cfg.maxRange, 0.0f, 1.0f);
    // Combined strength drives slowdown.  Rotational pull uses coneFactor
    // alone so that rotationalCompensation=1.0 gives perfect tracking at
    // any valid distance.
    const float strength = coneFactor * distFactor;

    if (coneFactor <= 0.0f) {
        // Outside the outer cone — nothing to do, but keep state alive so
        // the anchor persists as the player approaches the target.
        return;
    }

    // Target AABB derived from Position (centre) + CollisionShape::halfExtents,
    // with horizontal half-extents clamped to 18 so the aim-assist silhouette
    // is closer to the real character width rather than the full collision box.
    const glm::vec3 aaHalfExtents{
        std::min(bestHalfExtents.x, 18.0f), bestHalfExtents.y, std::min(bestHalfExtents.z, 18.0f)};
    const glm::vec3 aabbMin = bestTargetPos - aaHalfExtents;
    const glm::vec3 aabbMax = bestTargetPos + aaHalfExtents;

    // Detect target-switch (or first-ever frame): re-initialise state.
    const bool switchedTarget = (state.lastTarget != bestTarget);
    if (switchedTarget || !state.initialised) {
        const glm::vec3 hitWorld = aimassist::rayOntoAABB(eye, camFwd, aabbMin, aabbMax, bestDist);
        state.anchorLocal = hitWorld - bestTargetPos;
        state.lastTargetPos = bestTargetPos;
        state.lastEye = eye;
        state.lastTarget = bestTarget;
        state.initialised = true;
        // No previous frame yet → no pull this frame.  Slowdown still
        // applies (it depends only on current strength, not on history).
    }

    // ── Compute direction to target center for asymmetric slowdown ──────
    // Offset from crosshair to target centre in (yaw, pitch) space.
    const glm::vec3 dirToTarget = glm::normalize(bestTargetPos - eye);
    float targetYaw = 0.0f, targetPitch = 0.0f;
    aimassist::dirToYawPitch(dirToTarget, targetYaw, targetPitch);
    const float dYawToTarget = std::remainder(targetYaw - curYaw, glm::radians(360.0f));
    const float dPitchToTarget = targetPitch - curPitch;

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        // ── 1. Asymmetric slowdown — refund part of look input, but MORE
        //       when moving away from target centre and LESS when moving
        //       toward it.  Proximity to centre amplifies the effect (curve).
        if (alwaysActive || lookStickMag >= cfg.activationStickThresh) {
            // Stick direction in (yaw, pitch) camera-motion space.
            // runGamepadLook does: yaw -= rx*..., pitch += ry*...
            // So stick-induced camera motion direction is (-rx, +ry).
            const float stickYaw = -rx;
            const float stickPitch = ry;

            const float stickLen = std::sqrt(stickYaw * stickYaw + stickPitch * stickPitch);
            const float targetOffsetLen = std::sqrt(dYawToTarget * dYawToTarget + dPitchToTarget * dPitchToTarget);

            float dirDot = 0.0f;
            if (stickLen > 1e-4f && targetOffsetLen > 1e-4f) {
                // Normalised dot product: +1 = moving directly toward target
                // centre, -1 = directly away.
                dirDot = (stickYaw * dYawToTarget + stickPitch * dPitchToTarget) / (stickLen * targetOffsetLen);
                dirDot = std::clamp(dirDot, -1.0f, 1.0f);
            }

            // Proximity factor: how close the crosshair is to target centre.
            // 1.0 = dead centre, 0.0 = at outer cone edge.  Squared for a
            // curve that ramps up quickly near centre.
            const float outerRad = glm::radians(cfg.outerConeDeg);
            const float proximity = 1.0f - std::clamp(bestAngle / outerRad, 0.0f, 1.0f);
            const float proxCurve = proximity * proximity;

            // Asymmetric strength:
            //   dirDot > 0 (toward centre): raise effective slowdown toward 1.0
            //     (less friction — let player get on target easily)
            //   dirDot < 0 (away from centre): lower effective slowdown toward 0.0
            //     (more friction — resist leaving target)
            // Scaled by proximity curve so the effect is strongest near centre.
            float effectiveSlowdown = cfg.slowdownStrength;
            if (dirDot > 0.0f) {
                // Moving toward: lerp slowdown up toward 1.0 (less sticky)
                effectiveSlowdown = cfg.slowdownStrength + (1.0f - cfg.slowdownStrength) * dirDot * proxCurve;
            } else {
                // Moving away: lerp slowdown down toward 0.0 (more sticky)
                effectiveSlowdown = cfg.slowdownStrength * (1.0f + dirDot * proxCurve);
            }
            effectiveSlowdown = std::clamp(effectiveSlowdown, 0.0f, 1.0f);

            const float refund = (1.0f - effectiveSlowdown) * strength;
            snap.yaw += rx * lookSens * dt * refund;
            snap.pitch -= ry * lookSens * dt * refund;
        }

        // ── 2. Movement-tracking rotational pull.
        //       Skipped on the acquisition frame (no previous-frame anchor).
        //       Uses coneFactor only (NOT distFactor) — rotationalCompensation
        //       of 1.0 means perfect tracking regardless of distance, as long
        //       as the target is within the cone and range.
        if (state.initialised && !switchedTarget) {
            // Use the SAME anchor_local for both endpoints — that isolates
            // the contribution of (target movement) + (player translation),
            // excluding the contribution of the player's own *aim* drag.
            // Drag is supposed to redirect the anchor on the body, not
            // generate a phantom pull, so it's attributed only to the
            // anchor-update step at the end of this function.
            const glm::vec3 prevAnchorWorld = state.lastTargetPos + state.anchorLocal;
            const glm::vec3 curAnchorWorld = bestTargetPos + state.anchorLocal;

            const glm::vec3 dirPrev = glm::normalize(prevAnchorWorld - state.lastEye);
            const glm::vec3 dirCur = glm::normalize(curAnchorWorld - eye);

            float yawPrev = 0.0f, pitchPrev = 0.0f, yawCur = 0.0f, pitchCur = 0.0f;
            aimassist::dirToYawPitch(dirPrev, yawPrev, pitchPrev);
            aimassist::dirToYawPitch(dirCur, yawCur, pitchCur);

            // Wrap yaw delta to [-π, π] so we always rotate the short way.
            const float dYaw = std::remainder(yawCur - yawPrev, glm::radians(360.0f));
            const float dPitch = pitchCur - pitchPrev;

            // Compensate `rotationalCompensation` of the apparent motion,
            // scaled by coneFactor (smooth falloff at cone edge) but NOT by
            // distFactor — distance already gates target selection, and the
            // user expects 1.0 to mean "perfect tracking" at any valid range.
            // Capped by `maxPullRate` so a teleport spike can't fling the
            // camera off the player's actual aim.
            const float maxThisFrame = cfg.maxPullRate * dt;
            const float yawPull =
                std::clamp(cfg.rotationalCompensation * coneFactor * dYaw, -maxThisFrame, maxThisFrame);
            const float pitchPull =
                std::clamp(cfg.rotationalCompensation * coneFactor * dPitch, -maxThisFrame, maxThisFrame);
            snap.yaw += yawPull;
            snap.pitch += pitchPull;

            // Re-wrap / clamp to match runGamepadLook's invariants.
            snap.yaw = std::remainder(snap.yaw, glm::radians(360.0f));
            snap.pitch = std::clamp(snap.pitch, -glm::radians(89.0f), glm::radians(89.0f));
        }
    });

    // ── Update anchor for next frame ──────────────────────────────────────
    // The new camFwd already includes both player input (runGamepadLook)
    // AND the pull we just applied, so the anchor naturally tracks where
    // the player + assist combined ended up looking.  Clamped onto the
    // target AABB so it never drifts off the silhouette.
    float updatedYaw = curYaw, updatedPitch = curPitch;
    registry.view<LocalPlayer, InputSnapshot>().each([&](const InputSnapshot& snap) {
        updatedYaw = snap.yaw;
        updatedPitch = snap.pitch;
    });
    const float cosUpdated = std::cos(updatedPitch);
    const glm::vec3 camFwdAfter{
        std::sin(updatedYaw) * cosUpdated, -std::sin(updatedPitch), std::cos(updatedYaw) * cosUpdated};
    const glm::vec3 newHitWorld = aimassist::rayOntoAABB(eye, camFwdAfter, aabbMin, aabbMax, bestDist);
    state.anchorLocal = newHitWorld - bestTargetPos;
    state.lastTargetPos = bestTargetPos;
    state.lastEye = eye;
    state.lastTarget = bestTarget;
    state.initialised = true;
}

} // namespace systems
