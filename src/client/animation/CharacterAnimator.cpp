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

/// Fraction of the walk clip's natural duration over which we consider
/// locomotion "fully walking" at tms::k_walkSpeed (320 u/s); below that we
/// blend toward Idle.  (Tuned visually; the low-pass timescale below keeps
/// the band changes from flickering.)
constexpr float k_walkSpeedRef = 320.0f;        ///< tms::k_walkSpeed
constexpr float k_runSpeedRef = 530.0f;         ///< tms::k_sprintSpeed

constexpr float k_speedLowPassTau = 0.08f;      ///< Low-pass time constant for speed (s).
constexpr float k_modeCrossfadeSeconds = 0.15f; ///< Slide/WallRun ↔ locomotion crossfade (s).

/// Slot layout (fixed for clarity):
///  [0] loco primary     (Idle / Run / RunBackward / SlowRun)
///  [1] loco secondary   (Walk / Run during 1-D blend)
///  [2] override         (Slide / WallRun / debug clip)
///  [3] reserved         (future: additive upper-body layer)
constexpr size_t k_slotLocoA = 0;
constexpr size_t k_slotLocoB = 1;
constexpr size_t k_slotOverride = 2;

enum class Mode : uint8_t
{
    Locomotion,
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

} // namespace

struct CharacterAnimator::Impl
{
    // Shared non-owning data.
    const CharacterRig* rig = nullptr;
    const AnimationLibrary* library = nullptr;
    const ISkinningBackend* skinner = nullptr;

    // Per-sampler ozz state — one Context per slot so caches don't collide.
    std::array<ozz::animation::SamplingJob::Context, 4> contexts;
    std::array<std::vector<ozz::math::SoaTransform>, 4> perSamplerLocals;

    // Blended outputs.
    std::vector<ozz::math::SoaTransform> blendedLocals;
    std::vector<ozz::math::Float4x4> models;
    std::vector<glm::mat4> skinMats;

    // Public snapshot.
    std::array<ClipSampler, 4> samplers{};

    // Graph state.
    Mode currentMode = Mode::Locomotion;
    Mode previousMode = Mode::Locomotion;
    float modeBlendT = 1.0f;          ///< 0 = just switched (previous mode still dominant), 1 = fully in new mode.
    float groupWeightOverride = 0.0f; ///< Actual (smoothed) weight of the override group.
    float smoothedSpeed = 0.0f;
    float locomotionPhase = 0.0f;     ///< Shared loco time ratio ∈ [0, 1].
    float overrideTime = 0.0f;        ///< Independent time ratio for slot[2] (slide/wallrun/debug).

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

const std::array<ClipSampler, 4>& CharacterAnimator::samplers() const noexcept
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

/// @brief Pick the two dominant locomotion clips + their blend from current speed.
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
        // Idle ↔ Walk
        outA = ClipId::Idle;
        outB = ClipId::Walk;
        outBlend = std::clamp((speed - k_idleCutoff) / (k_walkSpeedRef - k_idleCutoff), 0.0f, 1.0f);
        return;
    }
    if (speed < k_runSpeedRef) {
        // Walk ↔ Run (or RunBackward)
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
    const float forwardSpeed = glm::dot(vhoriz, forward);

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
            // Clips not available in phase 1 — hold the last pose.
            targetMode = Mode::HoldPose;
            break;
        default:
            targetMode = Mode::Locomotion;
            break;
        }
    }

    // --- 3. Mode crossfade bookkeeping. ---
    if (targetMode != impl_->currentMode) {
        impl_->previousMode = impl_->currentMode;
        impl_->currentMode = targetMode;
        impl_->modeBlendT = 0.0f; // start a fresh crossfade from previous→current.

        // Restart the override clip whenever we cross into an override mode.
        if (targetMode == Mode::Slide || targetMode == Mode::WallRun || targetMode == Mode::DebugOverride)
            impl_->overrideTime = 0.0f;
    }
    if (impl_->modeBlendT < 1.0f) {
        impl_->modeBlendT = std::min(1.0f, impl_->modeBlendT + dt / k_modeCrossfadeSeconds);
    }
    const float tBlend = impl_->modeBlendT;

    // Helper: target override-group weight for a given mode.
    const auto overrideWeightFor = [](Mode m) -> float {
        return (m == Mode::Slide || m == Mode::WallRun || m == Mode::DebugOverride) ? 1.0f : 0.0f;
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

    // Phase sync: advance shared locomotion phase at (1 / blendedDuration) per sec.
    const float loopSec = blendedDuration(*impl_->library, locoA, locoB, locoBlend);
    impl_->locomotionPhase += dt / loopSec;
    if (impl_->locomotionPhase >= 1.0f)
        impl_->locomotionPhase -= std::floor(impl_->locomotionPhase);
    if (impl_->locomotionPhase < 0.0f)
        impl_->locomotionPhase = 0.0f;

    auto& s0 = impl_->samplers[k_slotLocoA];
    auto& s1 = impl_->samplers[k_slotLocoB];
    auto& s2 = impl_->samplers[k_slotOverride];
    auto& s3 = impl_->samplers[3];

    s0.id = locoA;
    s0.timeRatio = impl_->locomotionPhase;
    s0.weight = (1.0f - locoBlend) * groupLoco;
    s0.playbackSpeed = 1.0f;
    s0.active = (s0.weight > 1e-4f) && impl_->library->has(locoA);

    s1.id = locoB;
    s1.timeRatio = impl_->locomotionPhase;
    s1.weight = locoBlend * groupLoco;
    s1.playbackSpeed = 1.0f;
    s1.active = (s1.weight > 1e-4f) && impl_->library->has(locoB) && (locoB != locoA);

    // --- 5. Override slot. ---
    ClipId overrideClip = ClipId::_Count;
    float overrideSpeedMul = 1.0f;
    switch (impl_->currentMode) {
    case Mode::Slide:
        overrideClip = ClipId::Slide;
        break;
    case Mode::WallRun:
        overrideClip = ClipId::WallRun;
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
        case Mode::DebugOverride:
            overrideClip = impl_->debugOverrideId;
            break;
        default:
            break;
        }
    }

    if (overrideClip != ClipId::_Count && overrideClip != ClipId::_Count) {
        const float dur = impl_->library->duration(overrideClip);
        if (dur > 0.0f) {
            impl_->overrideTime += dt * overrideSpeedMul / dur;
            if (impl_->overrideTime >= 1.0f)
                impl_->overrideTime -= std::floor(impl_->overrideTime);
            if (impl_->overrideTime < 0.0f)
                impl_->overrideTime = 0.0f;
        }
        s2.id = overrideClip;
        s2.timeRatio = impl_->overrideTime;
        s2.weight = impl_->groupWeightOverride;
        s2.playbackSpeed = overrideSpeedMul;
        s2.active = (s2.weight > 1e-4f) && impl_->library->has(overrideClip);
        if (!impl_->library->has(overrideClip) && !impl_->missingClipLogged[static_cast<size_t>(overrideClip)]) {
            SDL_Log("CharacterAnimator: clip '%s' not loaded — skipping override", clipName(overrideClip));
            impl_->missingClipLogged[static_cast<size_t>(overrideClip)] = true;
        }
    } else {
        s2.active = false;
        s2.weight = 0.0f;
    }
    s3.active = false;
    s3.weight = 0.0f;

    // --- 6. Run SamplingJob for each active slot. ---
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

    // --- 7. BlendingJob: merge all active slots. ---
    if (activeCount == 0) {
        // Hold rest pose: copy rest pose into blended locals.
        const auto rest = impl_->rig->skeleton()->joint_rest_poses();
        std::copy(rest.begin(), rest.end(), impl_->blendedLocals.begin());
    } else {
        std::array<ozz::animation::BlendingJob::Layer, 4> layers;
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

    // --- 8. LocalToModelJob: flatten the hierarchy to per-joint model-space matrices. ---
    ozz::animation::LocalToModelJob l2m;
    l2m.skeleton = impl_->rig->skeleton();
    l2m.input = ozz::make_span(impl_->blendedLocals);
    l2m.output = ozz::make_span(impl_->models);
    if (!l2m.Run()) {
        SDL_Log("CharacterAnimator: local-to-model failed — skin matrices not updated");
        return;
    }

    // --- 9. Compose final skin matrices. ---
    const int nJoints = impl_->rig->numJoints();
    const auto& ibm = impl_->rig->inverseBindMatrices();
    for (int j = 0; j < nJoints; ++j) {
        const size_t uj = static_cast<size_t>(j);
        impl_->skinMats[uj] = anim_utils::ozzToGlm(impl_->models[uj]) * ibm[uj];
    }

    // Note: `crouching` is read from AnimationInputs but phase 1 has no
    // crouch-walk clip, so it's intentionally unused.  TODO(phase 2):
    // swap Walk → CrouchWalk when crouching + on-foot.
    (void)inputs.grounded;
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
