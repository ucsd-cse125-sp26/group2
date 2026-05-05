/// @file GamepadAimAssistSystem.hpp
/// @brief AAA-style controller aim assist — applied after runGamepadLook.
///
/// Implements the three pillars of modern console FPS aim assist:
///
///   1. **Rotational aim assist** — when the player rotates the camera and a
///      target sits inside an angular cone, the camera receives an additional
///      rotation toward the target each frame.  Strongest on the centre of
///      the cone, fading to zero at the cone edge.  This is the core feature
///      that keeps controller players competitive against mouse/keyboard.
///
///   2. **Target slowdown / aim friction** — when the crosshair is near a
///      target, we *refund* part of the player's just-applied stick input,
///      effectively reducing look sensitivity in that region.  Stops the
///      crosshair from flying past the enemy when the player flicks.
///
///   3. **Activation gate** — both effects are gated on the player actually
///      moving a stick (≥5% on either left or right stick by default).
///      Aim assist NEVER moves the camera while the player is holding still —
///      the player is in full control when stationary.  This is the standard
///      AAA convention (CoD, Apex, Halo all do it).
///
/// The system is fully client-side: it modifies `InputSnapshot.yaw/pitch` on
/// the local player just like the look sampler does, so the network/server
/// path sees adjusted angles transparently.  No server changes needed.
///
/// Mouse input is unaffected — the system early-outs when no gamepad is
/// connected, so kbm players never get a free target lock.

#pragma once

#include "InputSampleSystem.hpp" // for the gamepad::normaliseAxis helper + samplers
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Controllable.hpp"
#include "ecs/components/Hitbox.hpp"
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

namespace systems
{

/// @brief Tunable parameters for gamepad aim assist.
///
/// Exposed in the ECS inspector so testers can dial it in live.  The defaults
/// are tuned to feel like CoD / Apex "Standard" — noticeable but not glued.
struct GamepadAimAssistConfig
{
    bool enabled = true;

    /// @brief Inner cone (degrees from crosshair) where pull strength is at
    /// maximum.  Anywhere inside this cone, the full pull rate applies.
    float innerConeDeg = 3.0f;

    /// @brief Outer cone (degrees from crosshair).  Pull fades from 100 % at
    /// the inner cone edge to 0 % at the outer cone edge.  Larger = a bigger
    /// "magnet" but more obvious to the player when it kicks in.
    float outerConeDeg = 8.0f;

    /// @brief Maximum world distance to a target at which aim assist applies.
    /// Beyond this, snipers and long-range shots fall back to raw stick aim.
    float maxRange = 3000.0f;

    /// @brief Stick activation threshold (0..1 fraction of full deflection).
    /// Aim assist activates when EITHER the left (movement) OR right (look)
    /// stick is moved at least this much.  Default 0.05 = 5 %, matching the
    /// user's spec and the CoD/Apex convention.  Holding both sticks still
    /// disables all aim-assist effects so the player is in full control
    /// when stationary.
    float activationStickThresh = 0.05f;

    /// @brief Rotational pull rate at full strength, in radians per second.
    /// Scaled by the cone falloff and gated on the activation threshold.
    /// 1.5 rad/s ≈ 86°/s of free assist when on-target, which is meaningful
    /// without being obviously magnetic.
    float rotationalPullRate = 1.5f;

    /// @brief Slowdown factor (0..1).  When the crosshair is on a target the
    /// effective look sensitivity is multiplied by this much.  0.6 = look
    /// at 60 % speed inside the slowdown cone.  Implemented by refunding
    /// (1 − slowdownStrength) × inputDelta after runGamepadLook applied it.
    float slowdownStrength = 0.6f;

    /// @brief Prefer the head capsule when picking which point on a target
    /// to aim at.  Falls back to upper torso when no head capsule exists
    /// (e.g. early-spawned entity before animation rig populates).
    bool preferHead = true;
};

namespace aimassist
{

/// @brief Convert a world-space direction back into yaw/pitch matching the
/// renderer's convention used by `cachedCamFwd_`:
///   fwd = (sin(yaw)·cos(pitch),  −sin(pitch),  cos(yaw)·cos(pitch))
inline void dirToYawPitch(const glm::vec3& dir, float& outYaw, float& outPitch)
{
    outYaw = std::atan2(dir.x, dir.z);
    outPitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));
}

/// @brief Pick the best capsule on a target — head if available + preferred,
/// else the first upper-torso capsule, else the first capsule of any kind.
inline glm::vec3 pickAimPoint(const HitboxInstance& hb, bool preferHead)
{
    const WorldCapsule* head = nullptr;
    const WorldCapsule* torso = nullptr;
    for (const auto& cap : hb.capsules) {
        if (cap.region == BodyRegion::Head && !head)
            head = &cap;
        else if (cap.region == BodyRegion::UpperTorso && !torso)
            torso = &cap;
    }
    const WorldCapsule* pick =
        (preferHead && head) ? head : (torso ? torso : (hb.capsules.empty() ? nullptr : &hb.capsules.front()));
    if (!pick)
        return glm::vec3{0.0f}; // caller checks hb.capsules.empty() before reaching this anyway
    return 0.5f * (pick->pointA + pick->pointB);
}

/// @brief Smooth falloff from 1.0 inside the inner cone to 0.0 at the outer
/// cone edge.  Linear is fine here — the player can't perceive higher-order
/// curves through a stick at 60 Hz.
inline float coneFalloff(float angleRad, float innerRad, float outerRad)
{
    if (angleRad <= innerRad)
        return 1.0f;
    if (angleRad >= outerRad)
        return 0.0f;
    return (outerRad - angleRad) / (outerRad - innerRad);
}

} // namespace aimassist

/// @brief Apply gamepad aim assist (slowdown + rotational pull) to the local
/// player's InputSnapshot.  Must run AFTER `runGamepadLook` so we can refund
/// part of the player input it just integrated.
///
/// @param registry        ECS registry.
/// @param gamepad         Open gamepad handle (nullptr → no-op).
/// @param cfg             Tuning parameters.
/// @param lookSens        Same value passed to runGamepadLook (rad/s @ full deflection).
/// @param dt              Frame delta time (seconds).
inline void runGamepadAimAssist(
    Registry& registry, SDL_Gamepad* gamepad, const GamepadAimAssistConfig& cfg, float lookSens, float dt)
{
    if (!gamepad || !cfg.enabled)
        return;

    // ── Activation gate ───────────────────────────────────────────────────
    // Read all four stick axes once and check the magnitudes.  The gate fires
    // when EITHER stick is moved more than the threshold — matches the
    // user's spec and the AAA convention.
    const float lx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    const float ly = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    const float rx = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    const float ry = gamepad::normaliseAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));

    const float moveStickMag = std::sqrt(lx * lx + ly * ly);
    const float lookStickMag = std::sqrt(rx * rx + ry * ry);
    if (moveStickMag < cfg.activationStickThresh && lookStickMag < cfg.activationStickThresh)
        return;

    // ── Local player aim state ────────────────────────────────────────────
    // We use the snap.yaw/pitch that runGamepadLook just updated, plus the
    // local player's world position derived from the Position + CollisionShape
    // (so the eye position is fresh, not last-frame's cachedEye_).
    entt::entity localEntity = entt::null;
    glm::vec3 eye{0.0f};
    float curYaw = 0.0f, curPitch = 0.0f;
    bool foundLocal = false;
    registry.view<LocalPlayer, Position, CollisionShape, InputSnapshot>().each(
        [&](entt::entity e, const Position& pos, const CollisionShape& shape, const InputSnapshot& snap) {
            localEntity = e;
            // Eye sits at 77 % of the half-height — same factor used at line ~1443
            // in Game.cpp where the camera resolve computes renderEye.  Keep them
            // in sync; if eye-offset constants are ever centralised, replace.
            const float eyeOffset = shape.halfExtents.y * 0.77f;
            eye = pos.value + glm::vec3{0.0f, eyeOffset, 0.0f};
            curYaw = snap.yaw;
            curPitch = snap.pitch;
            foundLocal = true;
        });
    if (!foundLocal)
        return;

    // Current aim direction in the renderer's convention.
    const float cosPitch = std::cos(curPitch);
    const glm::vec3 camFwd{std::sin(curYaw) * cosPitch, -std::sin(curPitch), std::cos(curYaw) * cosPitch};

    // ── Target selection: smallest angular distance, range-gated, LOS-clear ──
    entt::entity bestTarget = entt::null;
    glm::vec3 bestAim{0.0f};
    float bestAngle = glm::radians(cfg.outerConeDeg);
    float bestDist = cfg.maxRange;

    const auto& world = physics::activeWorld();

    registry.view<Position, HitboxInstance, PlayerVisState>(entt::exclude<LocalPlayer>)
        .each([&](entt::entity e, const Position& /*pos*/, const HitboxInstance& hb, const PlayerVisState& pvis) {
            if (e == localEntity)
                return;
            if (pvis.isDead)
                return;
            if (registry.all_of<RespawnTimer>(e))
                return;
            if (hb.capsules.empty())
                return; // not yet animated this frame; skip rather than aim at world origin

            const glm::vec3 aimPoint = aimassist::pickAimPoint(hb, cfg.preferHead);
            const glm::vec3 toTarget = aimPoint - eye;
            const float dist = glm::length(toTarget);
            if (dist < 1e-3f || dist > cfg.maxRange)
                return;

            const glm::vec3 dirToTarget = toTarget / dist;
            const float dot = glm::clamp(glm::dot(camFwd, dirToTarget), -1.0f, 1.0f);
            if (dot <= 0.0f)
                return; // behind the camera

            const float angle = std::acos(dot);
            if (angle >= bestAngle)
                return;

            // Line-of-sight: reject if a wall is between us and the target.
            // Only test world geometry — testing against player hitboxes would
            // self-occlude (we'd hit the target's own capsule).
            const physics::HitscanHit blocker = physics::raycastWorld(eye, dirToTarget, world);
            if (blocker.hit && blocker.distance < dist - 1.0f)
                return;

            bestAngle = angle;
            bestTarget = e;
            bestAim = aimPoint;
            bestDist = dist;
        });

    if (bestTarget == entt::null)
        return;

    // Strength curve: cone falloff × distance falloff.  Both inputs are
    // already clamped, so the product is in [0, 1].
    const float coneFactor =
        aimassist::coneFalloff(bestAngle, glm::radians(cfg.innerConeDeg), glm::radians(cfg.outerConeDeg));
    const float distFactor = 1.0f - std::clamp(bestDist / cfg.maxRange, 0.0f, 1.0f);
    const float strength = coneFactor * distFactor;
    if (strength <= 0.0f)
        return;

    // Target yaw/pitch in the renderer convention.
    const glm::vec3 dirToBest = (bestAim - eye) / bestDist;
    float targetYaw = 0.0f, targetPitch = 0.0f;
    aimassist::dirToYawPitch(dirToBest, targetYaw, targetPitch);

    registry.view<InputSnapshot, LocalPlayer, Controllable>().each([&](InputSnapshot& snap) {
        // ── 1. Slowdown — refund part of the look input that runGamepadLook
        //       integrated this frame.  No-op when the right stick is below
        //       its activation threshold (player isn't actively aiming).
        if (lookStickMag >= cfg.activationStickThresh) {
            const float refund = (1.0f - cfg.slowdownStrength) * strength;
            //   runGamepadLook applied: snap.yaw -= rx * lookSens * dt;
            //                           snap.pitch += ry * lookSens * dt;
            // Refund `refund` fraction of those deltas (signs matched).
            snap.yaw += rx * lookSens * dt * refund;
            snap.pitch -= ry * lookSens * dt * refund;
        }

        // ── 2. Rotational pull — slew yaw/pitch toward the target at a
        //       rate capped by `rotationalPullRate`.  This is what makes
        //       the camera "stick" to a strafing target while the player
        //       rotates: the assist contributes some of the angular speed
        //       the player would otherwise have to provide themselves.
        // Wrap yaw error to [-π, π] so the pull always takes the short way
        // around rather than spinning a full 360° at the antimeridian.
        const float yawErr = std::remainder(targetYaw - snap.yaw, glm::radians(360.0f));
        const float pitchErr = targetPitch - snap.pitch;

        const float maxPullThisFrame = cfg.rotationalPullRate * strength * dt;
        const float yawPull = std::clamp(yawErr, -maxPullThisFrame, maxPullThisFrame);
        const float pitchPull = std::clamp(pitchErr, -maxPullThisFrame, maxPullThisFrame);
        snap.yaw += yawPull;
        snap.pitch += pitchPull;

        // Re-wrap and re-clamp to match runGamepadLook's invariants.
        snap.yaw = std::remainder(snap.yaw, glm::radians(360.0f));
        snap.pitch = std::clamp(snap.pitch, -glm::radians(89.0f), glm::radians(89.0f));
    });
}

} // namespace systems
