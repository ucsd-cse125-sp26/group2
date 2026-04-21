/// @file CharacterAnimator.cpp
/// @brief Per-entity animator: state machine + sampling + blending + skin matrices.

#include "CharacterAnimator.hpp"

#include "ecs/physics/TitanfallConstants.hpp"

#include <SDL3/SDL_log.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>

namespace
{

/// Phase 1 locomotion thresholds (u/s).  Tracks `tms` so the animation speed
/// bands match the physics movement speeds exactly.
constexpr float k_idleCutoff = 10.0f;

/// Reference speeds for the walk / run clips — used for speed-scaling so
/// foot contact frequency matches the character's actual world speed.
constexpr float k_walkSpeedRef = 320.0f;        ///< tms::k_walkSpeed
constexpr float k_runSpeedRef = 530.0f;         ///< tms::k_sprintSpeed

constexpr float k_speedLowPassTau = 0.08f;      ///< Low-pass time constant for speed (s).
constexpr float k_modeCrossfadeSeconds = 0.15f; ///< Slide/WallRun ↔ locomotion crossfade (s).

/// Slot layout (kNumSamplerSlots = 5):
///  [0] loco primary     (Idle / Walk / Run / RunBackward)
///  [1] loco secondary   (Walk / Run during 1-D speed band blend)
///  [2] loco strafe      (StrafeLeft / StrafeRight / walk variants)
///  [3] override         (Slide / WallRun / Jump / debug clip)
///  [4] reserved         (future: additive upper-body layer)
constexpr size_t k_slotLocoA = 0;
constexpr size_t k_slotLocoB = 1;
constexpr size_t k_slotStrafe = 2;
constexpr size_t k_slotOverride = 3;

enum class Mode : uint8_t
{
    Locomotion,
    Airborne, ///< In the air (not wallrunning/sliding/climbing).
    Slide,
    WallRun,
    HoldPose,
    DebugOverride,
};

/// @brief Values of `MoveMode` as stored in AnimationInputs::moveMode.
/// Kept in lockstep with the enum in src/ecs/components/PlayerState.hpp.
enum MoveModeValue
{
    MoveModeOnFoot = 0,
    MoveModeSliding = 1,
    MoveModeWallRunning = 2,
    MoveModeClimbing = 3,
    MoveModeLedgeGrabbing = 4,
};

/// WallSide values (lockstep with PlayerState.hpp).
enum WallSideValue
{
    WallSideNone = 0,
    WallSideLeft = 1,
    WallSideRight = 2,
};

} // namespace

struct CharacterAnimator::Impl
{
    // Shared non-owning data.
    const CharacterRig* rig = nullptr;
    const AnimationLibrary* library = nullptr;
    const ISkinningBackend* skinner = nullptr;

    // Per-sampler ozz state — one Context per slot so caches don't collide.
    std::array<ozz::animation::SamplingJob::Context, kNumSamplerSlots> contexts;
    std::array<std::vector<ozz::math::SoaTransform>, kNumSamplerSlots> perSamplerLocals;

    // Blended outputs.
    std::vector<ozz::math::SoaTransform> blendedLocals;
    std::vector<ozz::math::Float4x4> models;
    std::vector<glm::mat4> skinMats;

    // Public snapshot.
    std::array<ClipSampler, kNumSamplerSlots> samplers{};

    // Graph state.
    Mode currentMode = Mode::Locomotion;
    Mode previousMode = Mode::Locomotion;
    float modeBlendT = 1.0f;          ///< 0 = just switched (previous mode still dominant), 1 = fully in new mode.
    float groupWeightOverride = 0.0f; ///< Actual (smoothed) weight of the override group.
    float smoothedSpeed = 0.0f;
    float locomotionPhase = 0.0f;     ///< Shared loco time ratio in [0, 1].
    float overrideTime = 0.0f;        ///< Independent time ratio for the override slot.

    // Wallrun mirror state — true when the wallrun animation needs to be
    // mirrored in X so the character leans toward the correct wall side.
    bool wallRunMirror = false;

    // Debug override.
    ClipId debugOverrideId = ClipId::_Count;
    float debugPlaybackSpeedMul = 1.0f;

    // One-time "clip missing" log gate per clip slot.
    std::array<bool, static_cast<size_t>(ClipId::_Count)> missingClipLogged{};
};

CharacterAnimator::CharacterAnimator(const CharacterRig& rig, const AnimationLibrary& library)
    : impl_(std::make_unique<Impl>())
{
    impl_->rig = &rig;
    impl_->library = &library;

    const int numJoints = rig.numJoints();
    const int numSoaJoints = rig.skeleton() ? rig.skeleton()->num_soa_joints() : 0;

    for (size_t i = 0; i < impl_->contexts.size(); ++i) {
        impl_->contexts[i].Resize(numJoints);
        impl_->perSamplerLocals[i].resize(static_cast<size_t>(numSoaJoints));
    }
    impl_->blendedLocals.resize(static_cast<size_t>(numSoaJoints));
    impl_->models.resize(static_cast<size_t>(numJoints));
    impl_->skinMats.resize(static_cast<size_t>(numJoints), glm::mat4(1.0f));
}

CharacterAnimator::~CharacterAnimator() = default;

void CharacterAnimator::setSkinningBackend(const ISkinningBackend* backend)
{
    impl_->skinner = backend;
}

void CharacterAnimator::setDebugOverride(ClipId id, bool /*loop*/)
{
    if (id == impl_->debugOverrideId)
        return;
    impl_->debugOverrideId = id;
    impl_->overrideTime = 0.0f; // restart the override clip from t=0.
}

ClipId CharacterAnimator::debugOverride() const noexcept
{
    return impl_->debugOverrideId;
}

const std::array<ClipSampler, kNumSamplerSlots>& CharacterAnimator::samplers() const noexcept
{
    return impl_->samplers;
}

void CharacterAnimator::setDebugPlaybackSpeed(float mul) noexcept
{
    impl_->debugPlaybackSpeedMul = std::max(0.0f, mul);
}

int CharacterAnimator::numJoints() const noexcept
{
    return impl_->rig ? impl_->rig->numJoints() : 0;
}

namespace
{

/// @brief Pick the two dominant forward/backward locomotion clips + their blend from current speed.
/// @param speed          Smoothed horizontal speed.
/// @param forwardSpeed   Dot(velocity_horiz, forward).  Negative = moving backward.
/// @param outA / outB    Clip IDs for primary + secondary slot.
/// @param outBlend       Weight of B in [0,1]; weight of A is (1-blend).
void pickLocomotion(float speed, float forwardSpeed, ClipId& outA, ClipId& outB, float& outBlend)
{
    // Reverse-walk / reverse-run: substitute Run with RunBackward when moving backward.
    const bool reverseLike = (forwardSpeed < -k_idleCutoff);

    if (speed < k_idleCutoff) {
        outA = ClipId::Idle;
        outB = ClipId::Idle;
        outBlend = 0.0f;
        return;
    }
    if (speed < k_walkSpeedRef) {
        // Idle - Walk
        outA = ClipId::Idle;
        outB = ClipId::Walk;
        outBlend = std::clamp((speed - k_idleCutoff) / (k_walkSpeedRef - k_idleCutoff), 0.0f, 1.0f);
        return;
    }
    if (speed < k_runSpeedRef) {
        // Walk - Run (or RunBackward)
        outA = ClipId::Walk;
        outB = reverseLike ? ClipId::RunBackward : ClipId::Run;
        outBlend = std::clamp((speed - k_walkSpeedRef) / (k_runSpeedRef - k_walkSpeedRef), 0.0f, 1.0f);
        return;
    }
    // Run (or RunBackward) at cap.
    const ClipId runId = reverseLike ? ClipId::RunBackward : ClipId::Run;
    outA = runId;
    outB = runId;
    outBlend = 0.0f;
}

/// @brief Pick the strafe clip based on lateral speed and speed band.
/// @param rightSpeed     Signed lateral speed (positive = moving right).
/// @param speed          Total horizontal speed.
/// @return ClipId of the strafe clip, or _Count if no strafe is needed.
ClipId pickStrafeClip(float rightSpeed, float speed)
{
    if (std::abs(rightSpeed) < k_idleCutoff)
        return ClipId::_Count; // negligible strafe

    const bool right = (rightSpeed > 0.0f);
    if (speed < k_walkSpeedRef) {
        return right ? ClipId::StrafeRightWalk : ClipId::StrafeLeftWalk;
    }
    return right ? ClipId::StrafeRight : ClipId::StrafeLeft;
}

/// @brief Weighted-average loop duration used for phase-sync.
float blendedDuration(const AnimationLibrary& lib, ClipId a, ClipId b, float blend)
{
    const float durA = lib.duration(a);
    const float durB = lib.duration(b);
    // Fall back to the other clip if one has unknown duration.
    const float effA = (durA > 0.0f) ? durA : durB;
    const float effB = (durB > 0.0f) ? durB : durA;
    const float w = (1.0f - blend) * effA + blend * effB;
    return (w > 0.0f) ? w : 1.0f;
}

/// @brief Compute speed-scaling multiplier so the animation playback rate
/// matches the character's actual world speed.
///
/// The idea: each locomotion clip was authored at a specific reference speed
/// (Walk at ~320 u/s, Run at ~530 u/s).  When the character moves faster or
/// slower than the clip's reference, we speed up / slow down the animation
/// so foot contacts stay planted.
///
/// @param speed  Smoothed total horizontal speed (u/s).
/// @return Multiplier >= 0 for the locomotion phase advance rate.
float computeSpeedScale(float speed)
{
    if (speed < k_idleCutoff)
        return 1.0f; // Idle — no speed-scaling needed.

    float refSpeed;
    if (speed < k_walkSpeedRef) {
        // Below walk speed: the Walk clip is the dominant locomotion clip.
        // Slow the animation proportionally so feet don't slide.
        refSpeed = k_walkSpeedRef;
    } else if (speed < k_runSpeedRef) {
        // Walk-Run blend zone — reference interpolates linearly with speed.
        // Since the blend parameter t = (speed - walk) / (run - walk) and
        // refSpeed = lerp(walk, run, t) = speed, the result is always 1.0.
        // We compute it explicitly for clarity.
        const float t = (speed - k_walkSpeedRef) / (k_runSpeedRef - k_walkSpeedRef);
        refSpeed = k_walkSpeedRef + t * (k_runSpeedRef - k_walkSpeedRef);
    } else {
        // Above run speed — speed up the animation.
        refSpeed = k_runSpeedRef;
    }

    return speed / refSpeed;
}

} // namespace

void CharacterAnimator::update(const AnimationInputs& inputs, float dt)
{
    if (!impl_->rig || !impl_->rig->isLoaded() || !impl_->library)
        return;

    // --- 1. Low-pass smoothed speed for stable band selection. ---
    const glm::vec3 vhoriz{inputs.velocityWorld.x, 0.0f, inputs.velocityWorld.z};
    const float speed = glm::length(vhoriz);
    const float cosYaw = std::cos(inputs.yawRad);
    const float sinYaw = std::sin(inputs.yawRad);
    const glm::vec3 forward{sinYaw, 0.0f, cosYaw};
    const glm::vec3 right{cosYaw, 0.0f, -sinYaw};
    const float forwardSpeed = glm::dot(vhoriz, forward);
    const float rightSpeed = glm::dot(vhoriz, right);

    // Exponential smoothing: alpha = 1 - exp(-dt/tau).
    const float alpha = (dt > 0.0f) ? (1.0f - std::exp(-dt / k_speedLowPassTau)) : 0.0f;
    impl_->smoothedSpeed += (speed - impl_->smoothedSpeed) * alpha;

    // --- 2. Determine target mode. ---
    Mode targetMode = Mode::Locomotion;
    if (impl_->debugOverrideId != ClipId::_Count) {
        targetMode = Mode::DebugOverride;
    } else {
        switch (inputs.moveMode) {
        case MoveModeSliding:
            targetMode = Mode::Slide;
            break;
        case MoveModeWallRunning:
            targetMode = Mode::WallRun;
            break;
        case MoveModeClimbing:
        case MoveModeLedgeGrabbing:
            targetMode = Mode::HoldPose;
            break;
        default:
            // OnFoot: grounded → Locomotion, airborne → Airborne.
            if (!inputs.grounded)
                targetMode = Mode::Airborne;
            else
                targetMode = Mode::Locomotion;
            break;
        }
    }

    // --- 3. Mode crossfade bookkeeping. ---
    if (targetMode != impl_->currentMode) {
        impl_->previousMode = impl_->currentMode;
        impl_->currentMode = targetMode;
        impl_->modeBlendT = 0.0f; // start a fresh crossfade from previous->current.

        // Restart the override clip whenever we cross into an override mode.
        if (targetMode == Mode::Slide || targetMode == Mode::WallRun || targetMode == Mode::Airborne ||
            targetMode == Mode::DebugOverride)
            impl_->overrideTime = 0.0f;
    }
    if (impl_->modeBlendT < 1.0f) {
        impl_->modeBlendT = std::min(1.0f, impl_->modeBlendT + dt / k_modeCrossfadeSeconds);
    }
    const float tBlend = impl_->modeBlendT;

    // Helper: target override-group weight for a given mode.
    const auto overrideWeightFor = [](Mode m) -> float {
        return (m == Mode::Slide || m == Mode::WallRun || m == Mode::Airborne || m == Mode::DebugOverride) ? 1.0f
                                                                                                           : 0.0f;
    };
    const float prevOverride = overrideWeightFor(impl_->previousMode);
    const float targetOverride = overrideWeightFor(impl_->currentMode);
    impl_->groupWeightOverride = prevOverride + (targetOverride - prevOverride) * tBlend;
    const float groupLoco = 1.0f - impl_->groupWeightOverride;

    // --- 4. Locomotion slots (always computed for phase continuity). ---
    ClipId locoA = ClipId::Idle;
    ClipId locoB = ClipId::Idle;
    float locoBlend = 0.0f;
    pickLocomotion(impl_->smoothedSpeed, forwardSpeed, locoA, locoB, locoBlend);

    // Strafe clip and blend ratio.
    const ClipId strafeClip = pickStrafeClip(rightSpeed, impl_->smoothedSpeed);
    // strafeRatio: how much of the locomotion weight goes to strafe vs forward/backward.
    const float strafeRatio = (speed > k_idleCutoff && strafeClip != ClipId::_Count)
                                  ? std::clamp(std::abs(rightSpeed) / std::max(speed, 1.0f), 0.0f, 1.0f)
                                  : 0.0f;

    // Speed scaling: make the animation playback rate match the actual world speed.
    const float speedScale = computeSpeedScale(impl_->smoothedSpeed);

    // Phase sync: advance shared locomotion phase at (speedScale / blendedDuration) per sec.
    const float loopSec = blendedDuration(*impl_->library, locoA, locoB, locoBlend);
    impl_->locomotionPhase += dt * speedScale / loopSec;
    if (impl_->locomotionPhase >= 1.0f)
        impl_->locomotionPhase -= std::floor(impl_->locomotionPhase);
    if (impl_->locomotionPhase < 0.0f)
        impl_->locomotionPhase = 0.0f;

    auto& s0 = impl_->samplers[k_slotLocoA];
    auto& s1 = impl_->samplers[k_slotLocoB];
    auto& sStrafe = impl_->samplers[k_slotStrafe];
    auto& sOvr = impl_->samplers[k_slotOverride];
    auto& sReserved = impl_->samplers[4];

    // Forward / backward primary slot.
    s0.id = locoA;
    s0.timeRatio = impl_->locomotionPhase;
    s0.weight = (1.0f - locoBlend) * groupLoco * (1.0f - strafeRatio);
    s0.playbackSpeed = speedScale;
    s0.active = (s0.weight > 1e-4f) && impl_->library->has(locoA);

    // Forward / backward secondary slot (speed band blend).
    s1.id = locoB;
    s1.timeRatio = impl_->locomotionPhase;
    s1.weight = locoBlend * groupLoco * (1.0f - strafeRatio);
    s1.playbackSpeed = speedScale;
    s1.active = (s1.weight > 1e-4f) && impl_->library->has(locoB) && (locoB != locoA);

    // Strafe slot.
    sStrafe.id = strafeClip;
    sStrafe.timeRatio = impl_->locomotionPhase;
    sStrafe.weight = strafeRatio * groupLoco;
    sStrafe.playbackSpeed = speedScale;
    sStrafe.active = (sStrafe.weight > 1e-4f) && (strafeClip != ClipId::_Count) && impl_->library->has(strafeClip);

    // --- 5. Override slot (Slide / WallRun / Jump / Debug). ---
    ClipId overrideClip = ClipId::_Count;
    float overrideSpeedMul = 1.0f;
    switch (impl_->currentMode) {
    case Mode::Slide:
        overrideClip = ClipId::Slide;
        break;
    case Mode::WallRun:
        overrideClip = ClipId::WallRun;
        break;
    case Mode::Airborne:
        overrideClip = ClipId::Jump;
        break;
    case Mode::DebugOverride:
        overrideClip = impl_->debugOverrideId;
        overrideSpeedMul = impl_->debugPlaybackSpeedMul;
        break;
    default:
        break;
    }

    // During a crossfade FROM an override mode TO locomotion, keep the override
    // clip playing at its previous-mode ID so the fade-out is visible.
    if (overrideClip == ClipId::_Count && tBlend < 1.0f) {
        switch (impl_->previousMode) {
        case Mode::Slide:
            overrideClip = ClipId::Slide;
            break;
        case Mode::WallRun:
            overrideClip = ClipId::WallRun;
            break;
        case Mode::Airborne:
            overrideClip = ClipId::Jump;
            break;
        case Mode::DebugOverride:
            overrideClip = impl_->debugOverrideId;
            break;
        default:
            break;
        }
    }

    if (overrideClip != ClipId::_Count) {
        const float dur = impl_->library->duration(overrideClip);
        if (dur > 0.0f) {
            impl_->overrideTime += dt * overrideSpeedMul / dur;
            if (impl_->overrideTime >= 1.0f)
                impl_->overrideTime -= std::floor(impl_->overrideTime);
            if (impl_->overrideTime < 0.0f)
                impl_->overrideTime = 0.0f;
        }
        sOvr.id = overrideClip;
        sOvr.timeRatio = impl_->overrideTime;
        sOvr.weight = impl_->groupWeightOverride;
        sOvr.playbackSpeed = overrideSpeedMul;
        sOvr.active = (sOvr.weight > 1e-4f) && impl_->library->has(overrideClip);
        if (!impl_->library->has(overrideClip) && !impl_->missingClipLogged[static_cast<size_t>(overrideClip)]) {
            SDL_Log("CharacterAnimator: clip '%s' not loaded — skipping override", clipName(overrideClip));
            impl_->missingClipLogged[static_cast<size_t>(overrideClip)] = true;
        }
    } else {
        sOvr.active = false;
        sOvr.weight = 0.0f;
    }
    sReserved.active = false;
    sReserved.weight = 0.0f;

    // --- 6. Wallrun mirror state. ---
    // The wallrun animation is assumed to be authored for a RIGHT-wall run
    // (character leaning right).  When running on a LEFT wall we mirror the
    // pose by negating X in the model-space matrices so the lean flips.
    impl_->wallRunMirror =
        (impl_->currentMode == Mode::WallRun && inputs.wallRunSide == WallSideLeft) ||
        (impl_->previousMode == Mode::WallRun && tBlend < 1.0f && inputs.wallRunSide == WallSideLeft);

    // --- 7. Run SamplingJob for each active slot. ---
    int activeCount = 0;
    for (size_t i = 0; i < impl_->samplers.size(); ++i) {
        auto& samp = impl_->samplers[i];
        if (!samp.active)
            continue;
        const ozz::animation::Animation* clip = impl_->library->get(samp.id);
        if (!clip) {
            samp.active = false;
            continue;
        }

        ozz::animation::SamplingJob job;
        job.animation = clip;
        job.context = &impl_->contexts[i];
        job.ratio = samp.timeRatio;
        job.output = ozz::make_span(impl_->perSamplerLocals[i]);
        if (!job.Run()) {
            SDL_Log("CharacterAnimator: sampling job failed for '%s'", clipName(samp.id));
            samp.active = false;
            continue;
        }
        ++activeCount;
    }

    // --- 8. BlendingJob: merge all active slots. ---
    if (activeCount == 0) {
        // Hold rest pose: copy rest pose into blended locals.
        const auto rest = impl_->rig->skeleton()->joint_rest_poses();
        std::copy(rest.begin(), rest.end(), impl_->blendedLocals.begin());
    } else {
        std::array<ozz::animation::BlendingJob::Layer, kNumSamplerSlots> layers;
        int layerCount = 0;
        for (size_t i = 0; i < impl_->samplers.size(); ++i) {
            const auto& samp = impl_->samplers[i];
            if (!samp.active)
                continue;
            layers[static_cast<size_t>(layerCount)].weight = samp.weight;
            layers[static_cast<size_t>(layerCount)].transform = ozz::make_span(impl_->perSamplerLocals[i]);
            ++layerCount;
        }

        ozz::animation::BlendingJob blend;
        blend.threshold = 0.1f;
        blend.layers =
            ozz::span<const ozz::animation::BlendingJob::Layer>(layers.data(), static_cast<size_t>(layerCount));
        blend.rest_pose = impl_->rig->skeleton()->joint_rest_poses();
        blend.output = ozz::make_span(impl_->blendedLocals);
        if (!blend.Validate()) {
            SDL_Log("CharacterAnimator: BlendingJob::Validate failed (layers=%d) — drawing rest pose", layerCount);
            const auto rest = impl_->rig->skeleton()->joint_rest_poses();
            std::copy(rest.begin(), rest.end(), impl_->blendedLocals.begin());
        } else if (!blend.Run()) {
            SDL_Log("CharacterAnimator: BlendingJob::Run failed — drawing rest pose");
            const auto rest = impl_->rig->skeleton()->joint_rest_poses();
            std::copy(rest.begin(), rest.end(), impl_->blendedLocals.begin());
        }
    }

    // --- 9. LocalToModelJob: flatten the hierarchy to per-joint model-space matrices. ---
    ozz::animation::LocalToModelJob l2m;
    l2m.skeleton = impl_->rig->skeleton();
    l2m.input = ozz::make_span(impl_->blendedLocals);
    l2m.output = ozz::make_span(impl_->models);
    if (!l2m.Run()) {
        SDL_Log("CharacterAnimator: local-to-model failed — skin matrices not updated");
        return;
    }

    // --- 10. Compose final skin matrices, with optional wallrun mirror. ---
    //
    // When wallrunning on the left wall, we apply a scale(-1, 1, 1) pre-
    // transform to all model-space joint matrices.  This mirrors the entire
    // skeleton in X so the character leans toward the correct wall.
    const int nJoints = impl_->rig->numJoints();
    const auto& ibm = impl_->rig->inverseBindMatrices();

    glm::mat4 mirrorMat(1.0f);
    if (impl_->wallRunMirror)
        mirrorMat[0][0] = -1.0f; // negate X axis.

    for (int j = 0; j < nJoints; ++j) {
        const size_t uj = static_cast<size_t>(j);
        impl_->skinMats[uj] = mirrorMat * anim_utils::ozzToGlm(impl_->models[uj]) * ibm[uj];
    }

    (void)inputs.sprinting;
    (void)inputs.crouching;
}

void CharacterAnimator::computeSkinnedVertices(std::vector<std::vector<ModelVertex>>& out) const
{
    if (!impl_->rig || !impl_->skinner) {
        out.clear();
        return;
    }
    const auto& meshes = impl_->rig->meshes();
    out.resize(meshes.size());
    for (size_t m = 0; m < meshes.size(); ++m) {
        impl_->skinner->skin(impl_->skinMats, meshes[m].baseVertices, meshes[m].skinWeights, out[m]);
    }
}
