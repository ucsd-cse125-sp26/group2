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
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

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
constexpr float k_dirLowPassTau = 0.10f;        ///< Low-pass time constant for directional components (s).
constexpr float k_modeCrossfadeSeconds = 0.15f; ///< Slide/WallRun ↔ locomotion crossfade (s).
constexpr float k_headPitchMax = 1.0472f;       ///< Max head pitch magnitude (~60 degrees).

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
    Crouch,
    Airborne, ///< In the air (not wallrunning/sliding).
    Slide,
    WallRun,
    HoldPose,
    DebugOverride,
};

/// @brief Values of `MoveMode` as stored in AnimationInputs::moveMode.
/// Kept in lockstep with the enum in src/ecs/components/PlayerStateEnums.hpp.
enum MoveModeValue
{
    MoveModeOnFoot = 0,
    MoveModeSliding = 1,
    MoveModeWallRunning = 2,
};

/// WallSide values (lockstep with PlayerState.hpp).
enum WallSideValue
{
    WallSideNone = 0,
    WallSideLeft = 1,
    WallSideRight = 2,
};

struct ArmIkChain
{
    int upperArm = -1;
    int foreArm = -1;
    int hand = -1;
    std::vector<bool> upperDescendants;
    std::vector<bool> foreDescendants;
    std::vector<bool> handDescendants;
    struct FingerIkChain
    {
        std::array<int, 4> joints{-1, -1, -1, -1};
        std::array<std::vector<bool>, 4> descendants;

        [[nodiscard]] bool valid() const noexcept
        {
            return joints[0] >= 0 && joints[1] >= 0 && joints[2] >= 0 && joints[3] >= 0 && !descendants[0].empty() &&
                   !descendants[1].empty() && !descendants[2].empty();
        }
    };
    std::array<FingerIkChain, kHandFingerIkCount> fingers{};

    [[nodiscard]] bool valid() const noexcept
    {
        return upperArm >= 0 && foreArm >= 0 && hand >= 0 && !upperDescendants.empty() && !foreDescendants.empty() &&
               !handDescendants.empty();
    }
};

glm::vec3 matrixTranslation(const glm::mat4& m)
{
    return glm::vec3(m[3]);
}

glm::vec3 normalizedOr(const glm::vec3& v, const glm::vec3& fallback)
{
    const float len = glm::length(v);
    return len > 0.0001f ? v / len : fallback;
}

glm::quat rotationBetween(glm::vec3 from, glm::vec3 to)
{
    from = normalizedOr(from, glm::vec3{1.0f, 0.0f, 0.0f});
    to = normalizedOr(to, from);
    const float cosTheta = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
    if (cosTheta > 0.9995f)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (cosTheta < -0.9995f) {
        glm::vec3 axis = glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, from);
        if (glm::dot(axis, axis) < 0.0001f)
            axis = glm::cross(glm::vec3{1.0f, 0.0f, 0.0f}, from);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + cosTheta, axis.x, axis.y, axis.z));
}

glm::mat4 rotateAround(const glm::vec3& pivot, const glm::quat& rotation)
{
    return glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(rotation) * glm::translate(glm::mat4(1.0f), -pivot);
}

std::vector<bool> buildDescendantMask(const ozz::animation::Skeleton* skeleton, int root)
{
    std::vector<bool> mask;
    if (skeleton == nullptr || root < 0)
        return mask;

    const int jointCount = skeleton->num_joints();
    mask.assign(static_cast<size_t>(jointCount), false);
    mask[static_cast<size_t>(root)] = true;

    const auto parents = skeleton->joint_parents();
    for (int joint = 0; joint < jointCount; ++joint) {
        int parent = static_cast<int>(parents[static_cast<size_t>(joint)]);
        while (parent >= 0) {
            if (parent == root) {
                mask[static_cast<size_t>(joint)] = true;
                break;
            }
            parent = static_cast<int>(parents[static_cast<size_t>(parent)]);
        }
    }
    return mask;
}

void applyDeltaToMask(std::vector<glm::mat4>& matrices, const std::vector<bool>& mask, const glm::mat4& delta)
{
    const size_t count = std::min(matrices.size(), mask.size());
    for (size_t i = 0; i < count; ++i) {
        if (mask[i])
            matrices[i] = delta * matrices[i];
    }
}

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
    std::vector<glm::mat4> jointModelMats; ///< Model-space matrices with procedural xforms, no IBM.

    // Public snapshot.
    std::array<ClipSampler, kNumSamplerSlots> samplers{};

    // Graph state.
    Mode currentMode = Mode::Locomotion;
    Mode previousMode = Mode::Locomotion;
    float modeBlendT = 1.0f;           ///< 0 = just switched (previous mode still dominant), 1 = fully in new mode.
    float groupWeightOverride = 0.0f;  ///< Actual (smoothed) weight of the override group.
    float smoothedSpeed = 0.0f;
    float smoothedForwardSpeed = 0.0f; ///< Low-pass filtered forward velocity component (u/s).
    float smoothedRightSpeed = 0.0f;   ///< Low-pass filtered rightward velocity component (u/s).
    float locomotionPhase = 0.0f;      ///< Shared loco time ratio in [0, 1].
    float overrideTime = 0.0f;         ///< Independent time ratio for the override slot.

    // Wallrun mirror state — true when the wallrun animation needs to be
    // mirrored in X so the character leans toward the correct wall side.
    bool wallRunMirror = false;

    // Head-look procedural pitch.
    int headJointIdx = -1;              ///< Runtime index of "mixamorig:Head" (-1 = not found).
    std::vector<bool> isHeadDescendant; ///< Per-joint flag: true for head + all children.

    // Weapon grip IK chains.
    ArmIkChain leftArm;
    ArmIkChain rightArm;

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
    impl_->jointModelMats.resize(static_cast<size_t>(numJoints), glm::mat4(1.0f));

    // Cache head joint index and build descendant mask for procedural head-look.
    if (rig.isLoaded() && rig.skeleton()) {
        const auto& jm = rig.jointMap();
        auto headIt = jm.find("mixamorig:Head");
        if (headIt != jm.end()) {
            impl_->headJointIdx = headIt->second;

            impl_->isHeadDescendant.assign(static_cast<size_t>(numJoints), false);
            impl_->isHeadDescendant[static_cast<size_t>(impl_->headJointIdx)] = true;

            // Walk the parent chain of every joint; if the head is an ancestor, mark it.
            const auto parents = rig.skeleton()->joint_parents();
            for (int j = 0; j < numJoints; ++j) {
                if (j == impl_->headJointIdx)
                    continue;
                int p = static_cast<int>(parents[static_cast<size_t>(j)]);
                while (p >= 0) {
                    if (p == impl_->headJointIdx) {
                        impl_->isHeadDescendant[static_cast<size_t>(j)] = true;
                        break;
                    }
                    p = static_cast<int>(parents[static_cast<size_t>(p)]);
                }
            }

            int descCount = 0;
            for (bool b : impl_->isHeadDescendant)
                if (b)
                    ++descCount;
            SDL_Log("CharacterAnimator: head joint '%s' at index %d, %d descendants",
                    "mixamorig:Head",
                    impl_->headJointIdx,
                    descCount - 1);
        }

        auto makeFingerChain = [&](const char* prefix) {
            ArmIkChain::FingerIkChain finger;
            for (int i = 0; i < 4; ++i) {
                const std::string jointName = std::string(prefix) + std::to_string(i + 1);
                const auto it = jm.find(jointName);
                if (it == jm.end())
                    return finger;
                finger.joints[static_cast<size_t>(i)] = it->second;
                finger.descendants[static_cast<size_t>(i)] = buildDescendantMask(rig.skeleton(), it->second);
            }
            return finger;
        };

        auto makeArmChain = [&](const char* side) {
            ArmIkChain chain;
            const std::string sidePrefix = std::string("mixamorig:") + side;
            const auto upperIt = jm.find(sidePrefix + "Arm");
            const auto foreIt = jm.find(sidePrefix + "ForeArm");
            const auto handIt = jm.find(sidePrefix + "Hand");
            if (upperIt == jm.end() || foreIt == jm.end() || handIt == jm.end())
                return chain;
            chain.upperArm = upperIt->second;
            chain.foreArm = foreIt->second;
            chain.hand = handIt->second;
            chain.upperDescendants = buildDescendantMask(rig.skeleton(), chain.upperArm);
            chain.foreDescendants = buildDescendantMask(rig.skeleton(), chain.foreArm);
            chain.handDescendants = buildDescendantMask(rig.skeleton(), chain.hand);
            chain.fingers[0] = makeFingerChain((sidePrefix + "HandThumb").c_str());
            chain.fingers[1] = makeFingerChain((sidePrefix + "HandIndex").c_str());
            chain.fingers[2] = makeFingerChain((sidePrefix + "HandMiddle").c_str());
            chain.fingers[3] = makeFingerChain((sidePrefix + "HandRing").c_str());
            chain.fingers[4] = makeFingerChain((sidePrefix + "HandPinky").c_str());
            return chain;
        };
        impl_->leftArm = makeArmChain("Left");
        impl_->rightArm = makeArmChain("Right");
    }
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

void CharacterAnimator::applyHandIkTargets(const HandIkTargets& targets)
{
    if (!impl_->rig || impl_->jointModelMats.empty() || impl_->skinMats.empty())
        return;

    auto solveArm = [&](const ArmIkChain& chain, const ArmIkTarget& target) {
        if (!target.enabled || !chain.valid())
            return false;

        const glm::vec3 shoulder = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(chain.upperArm)]);
        const glm::vec3 elbow = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(chain.foreArm)]);
        const glm::vec3 wrist = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(chain.hand)]);

        const float upperLen = glm::length(elbow - shoulder);
        const float foreLen = glm::length(wrist - elbow);
        if (upperLen < 0.0001f || foreLen < 0.0001f)
            return false;

        glm::vec3 targetPos = target.positionModel;
        glm::vec3 toTarget = targetPos - shoulder;
        float targetDist = glm::length(toTarget);
        if (targetDist < 0.0001f)
            return false;

        const float maxReach = std::max(0.0001f, upperLen + foreLen - 0.001f);
        const float minReach = std::max(0.0001f, std::abs(upperLen - foreLen) + 0.001f);
        const float solvedDist = std::clamp(targetDist, minReach, maxReach);
        const glm::vec3 reachDir = toTarget / targetDist;
        targetPos = shoulder + reachDir * solvedDist;

        const glm::vec3 currentPoleBase = shoulder + reachDir * glm::dot(elbow - shoulder, reachDir);
        glm::vec3 pole = target.elbowEnabled ? target.elbowPositionModel - currentPoleBase : elbow - currentPoleBase;
        if (glm::dot(pole, pole) < 0.0001f) {
            pole = glm::cross(reachDir, glm::vec3{0.0f, 1.0f, 0.0f});
            if (glm::dot(pole, pole) < 0.0001f)
                pole = glm::cross(reachDir, glm::vec3{1.0f, 0.0f, 0.0f});
        }
        pole = glm::normalize(pole);

        const float along = std::clamp(
            (upperLen * upperLen + solvedDist * solvedDist - foreLen * foreLen) / (2.0f * solvedDist), 0.0f, upperLen);
        const float height = std::sqrt(std::max(0.0f, upperLen * upperLen - along * along));
        const glm::vec3 solvedElbow = shoulder + reachDir * along + pole * height;

        const glm::quat upperRot = rotationBetween(elbow - shoulder, solvedElbow - shoulder);
        applyDeltaToMask(impl_->jointModelMats, chain.upperDescendants, rotateAround(shoulder, upperRot));

        const glm::vec3 currentElbow = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(chain.foreArm)]);
        const glm::vec3 currentWrist = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(chain.hand)]);
        const glm::quat foreRot = rotationBetween(currentWrist - currentElbow, targetPos - currentElbow);
        applyDeltaToMask(impl_->jointModelMats, chain.foreDescendants, rotateAround(currentElbow, foreRot));
        return true;
    };

    auto orientHand = [&](const ArmIkChain& chain, const ArmIkTarget& target) {
        if (!target.orientationEnabled || !chain.valid())
            return false;

        const size_t handIdx = static_cast<size_t>(chain.hand);
        const glm::vec3 wrist = matrixTranslation(impl_->jointModelMats[handIdx]);
        const glm::quat current = glm::normalize(glm::quat_cast(glm::mat3(impl_->jointModelMats[handIdx])));
        const glm::quat desired = glm::normalize(target.orientationModel);
        const glm::quat delta = desired * glm::inverse(current);
        applyDeltaToMask(impl_->jointModelMats, chain.handDescendants, rotateAround(wrist, delta));
        return true;
    };

    auto forcePalm = [&](const ArmIkChain& chain, const ArmIkTarget& target) {
        if (!target.enabled || !chain.valid())
            return false;

        const size_t handIdx = static_cast<size_t>(chain.hand);
        const glm::vec3 wrist = matrixTranslation(impl_->jointModelMats[handIdx]);
        const glm::vec3 delta = target.positionModel - wrist;
        if (glm::dot(delta, delta) < 0.000001f)
            return false;

        applyDeltaToMask(impl_->jointModelMats, chain.handDescendants, glm::translate(glm::mat4(1.0f), delta));
        return true;
    };

    auto solveFinger = [&](const ArmIkChain::FingerIkChain& chain, const glm::vec3& targetPos) {
        if (!chain.valid())
            return false;

        bool changed = false;
        for (int iter = 0; iter < 3; ++iter) {
            for (int jointSlot = 2; jointSlot >= 0; --jointSlot) {
                const int jointIdx = chain.joints[static_cast<size_t>(jointSlot)];
                const int tipIdx = chain.joints[3];
                const glm::vec3 jointPos = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(jointIdx)]);
                const glm::vec3 tipPos = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(tipIdx)]);
                const glm::vec3 toTip = tipPos - jointPos;
                const glm::vec3 toTarget = targetPos - jointPos;
                if (glm::length(toTip) < 0.0001f || glm::length(toTarget) < 0.0001f)
                    continue;
                const glm::quat rot = rotationBetween(toTip, toTarget);
                applyDeltaToMask(impl_->jointModelMats,
                                 chain.descendants[static_cast<size_t>(jointSlot)],
                                 rotateAround(jointPos, rot));
                changed = true;
            }
        }
        return changed;
    };

    auto solveFingers = [&](const ArmIkChain& chain, const ArmIkTarget& target) {
        bool changed = false;
        for (size_t i = 0; i < kHandFingerIkCount; ++i) {
            if (target.fingerEnabled[i])
                changed |= solveFinger(chain.fingers[i], target.fingerPositionsModel[i]);
        }
        return changed;
    };

    bool changedLeft = solveArm(impl_->leftArm, targets.left);
    bool changedRight = solveArm(impl_->rightArm, targets.right);
    changedLeft |= forcePalm(impl_->leftArm, targets.left);
    changedRight |= forcePalm(impl_->rightArm, targets.right);
    changedLeft |= orientHand(impl_->leftArm, targets.left);
    changedRight |= orientHand(impl_->rightArm, targets.right);
    changedLeft |= solveFingers(impl_->leftArm, targets.left);
    changedRight |= solveFingers(impl_->rightArm, targets.right);
    if (!changedLeft && !changedRight)
        return;

    const std::vector<glm::mat4>& inverseBind = impl_->rig->inverseBindMatrices();
    const size_t count = std::min(impl_->skinMats.size(), inverseBind.size());
    for (size_t i = 0; i < count; ++i)
        impl_->skinMats[i] = impl_->jointModelMats[i] * inverseBind[i];
}

int CharacterAnimator::numJoints() const noexcept
{
    return impl_->rig ? impl_->rig->numJoints() : 0;
}

const std::vector<glm::mat4>& CharacterAnimator::jointModelMatrices() const noexcept
{
    return impl_->jointModelMats;
}

const std::vector<glm::mat4>& CharacterAnimator::skinMatrices() const noexcept
{
    return impl_->skinMats;
}

namespace
{

/// @brief Pick the two dominant forward/backward locomotion clips + their blend from current speed.
/// @param speed              Smoothed horizontal speed.
/// @param smoothedForward    Low-pass filtered forward velocity component.
/// @param outA / outB        Clip IDs for primary + secondary slot.
/// @param outBlend           Weight of B in [0,1]; weight of A is (1-blend).
void pickLocomotion(float speed, float smoothedForward, ClipId& outA, ClipId& outB, float& outBlend)
{
    // Moving backward when the smoothed forward component is clearly negative.
    const bool reverseLike = (smoothedForward < -k_idleCutoff);

    if (speed < k_idleCutoff) {
        outA = ClipId::Idle;
        outB = ClipId::Idle;
        outBlend = 0.0f;
        return;
    }
    if (speed < k_walkSpeedRef) {
        // Idle - Walk (or Idle - RunBackward when moving backward).
        outA = ClipId::Idle;
        outB = reverseLike ? ClipId::RunBackward : ClipId::Walk;
        outBlend = std::clamp((speed - k_idleCutoff) / (k_walkSpeedRef - k_idleCutoff), 0.0f, 1.0f);
        return;
    }
    if (speed < k_runSpeedRef) {
        // Walk - Run (or RunBackward at any speed in the run band).
        outA = reverseLike ? ClipId::RunBackward : ClipId::Walk;
        outB = reverseLike ? ClipId::RunBackward : ClipId::Run;
        outBlend =
            reverseLike ? 0.0f : std::clamp((speed - k_walkSpeedRef) / (k_runSpeedRef - k_walkSpeedRef), 0.0f, 1.0f);
        return;
    }
    // Run (or RunBackward) at cap.
    const ClipId runId = reverseLike ? ClipId::RunBackward : ClipId::Run;
    outA = runId;
    outB = runId;
    outBlend = 0.0f;
}

/// @brief Pick the strafe clip based on lateral speed and speed band.
///
/// NOTE: the clip names in the FBX pack ("left strafe", "right strafe") are
/// from the *animation's* visual perspective.  In practice the left-named
/// clip shows the character leaning / stepping right and vice-versa, so the
/// mapping is intentionally crossed here to match the actual player input.
///
/// @param smoothedRight  Low-pass filtered rightward velocity component.
/// @param speed          Smoothed total horizontal speed.
/// @return ClipId of the strafe clip, or _Count if no strafe is needed.
ClipId pickStrafeClip(float smoothedRight, float speed)
{
    if (std::abs(smoothedRight) < k_idleCutoff)
        return ClipId::_Count; // negligible strafe

    // Swapped: positive rightSpeed (player moving right) uses Left-named clip
    // because the "left strafe" animation visually moves the character rightward.
    const bool right = (smoothedRight > 0.0f);
    if (speed < k_walkSpeedRef) {
        return right ? ClipId::StrafeLeftWalk : ClipId::StrafeRightWalk;
    }
    return right ? ClipId::StrafeLeft : ClipId::StrafeRight;
}

/// @brief Pick crouch forward/backward locomotion clips + blend from current speed.
void pickCrouchLocomotion(float speed, float smoothedForward, ClipId& outA, ClipId& outB, float& outBlend)
{
    const bool reverseLike = (smoothedForward < -k_idleCutoff);

    if (speed < k_idleCutoff) {
        outA = ClipId::CrouchIdle;
        outB = ClipId::CrouchIdle;
        outBlend = 0.0f;
        return;
    }
    // Crouch only has walk-speed clips — blend from idle to walk, capped at walkSpeedRef.
    outA = ClipId::CrouchIdle;
    outB = reverseLike ? ClipId::CrouchWalkBackward : ClipId::CrouchWalk;
    outBlend = std::clamp((speed - k_idleCutoff) / (k_walkSpeedRef - k_idleCutoff), 0.0f, 1.0f);
}

/// @brief Pick crouch strafe clip based on lateral speed.
ClipId pickCrouchStrafeClip(float smoothedRight)
{
    if (std::abs(smoothedRight) < k_idleCutoff)
        return ClipId::_Count;
    return (smoothedRight > 0.0f) ? ClipId::CrouchWalkLeft : ClipId::CrouchWalkRight;
}

/// @brief Weighted-average loop duration used for phase-sync.
float blendedDuration(const AnimationLibrary& lib, ClipId a, ClipId b, float blend)
{
    const float durA = lib.duration(a);
    const float durB = lib.duration(b);
    const float effA = (durA > 0.0f) ? durA : durB;
    const float effB = (durB > 0.0f) ? durB : durA;
    const float w = (1.0f - blend) * effA + blend * effB;
    return (w > 0.0f) ? w : 1.0f;
}

/// @brief Compute speed-scaling multiplier so the animation playback rate
/// matches the character's actual world speed.
float computeSpeedScale(float speed)
{
    if (speed < k_idleCutoff)
        return 1.0f;

    float refSpeed;
    if (speed < k_walkSpeedRef) {
        refSpeed = k_walkSpeedRef;
    } else if (speed < k_runSpeedRef) {
        const float t = (speed - k_walkSpeedRef) / (k_runSpeedRef - k_walkSpeedRef);
        refSpeed = k_walkSpeedRef + t * (k_runSpeedRef - k_walkSpeedRef);
    } else {
        refSpeed = k_runSpeedRef;
    }

    return speed / refSpeed;
}

} // namespace

void CharacterAnimator::update(const AnimationInputs& inputs, float dt)
{
    if (!impl_->rig || !impl_->rig->isLoaded() || !impl_->library)
        return;

    // --- 1. Low-pass smoothed speed + directional components. ---
    const glm::vec3 vhoriz{inputs.velocityWorld.x, 0.0f, inputs.velocityWorld.z};
    const float speed = glm::length(vhoriz);
    const float cosYaw = std::cos(inputs.yawRad);
    const float sinYaw = std::sin(inputs.yawRad);
    const glm::vec3 forward{sinYaw, 0.0f, cosYaw};
    const glm::vec3 right{cosYaw, 0.0f, -sinYaw};
    const float forwardSpeed = glm::dot(vhoriz, forward);
    const float rightSpeed = glm::dot(vhoriz, right);

    // Exponential smoothing for total speed.
    const float alphaSpd = (dt > 0.0f) ? (1.0f - std::exp(-dt / k_speedLowPassTau)) : 0.0f;
    impl_->smoothedSpeed += (speed - impl_->smoothedSpeed) * alphaSpd;

    // Separate smoothing for directional components — prevents abrupt clip
    // switches when the player changes direction (e.g. forward → backward).
    const float alphaDir = (dt > 0.0f) ? (1.0f - std::exp(-dt / k_dirLowPassTau)) : 0.0f;
    impl_->smoothedForwardSpeed += (forwardSpeed - impl_->smoothedForwardSpeed) * alphaDir;
    impl_->smoothedRightSpeed += (rightSpeed - impl_->smoothedRightSpeed) * alphaDir;

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
        default:
            if (!inputs.grounded)
                targetMode = Mode::Airborne;
            else if (inputs.crouching)
                targetMode = Mode::Crouch;
            else
                targetMode = Mode::Locomotion;
            break;
        }
    }

    // --- 3. Mode crossfade bookkeeping. ---
    if (targetMode != impl_->currentMode) {
        impl_->previousMode = impl_->currentMode;
        impl_->currentMode = targetMode;
        impl_->modeBlendT = 0.0f;

        if (targetMode == Mode::Slide || targetMode == Mode::WallRun || targetMode == Mode::Airborne ||
            targetMode == Mode::DebugOverride)
            impl_->overrideTime = 0.0f;
    }
    if (impl_->modeBlendT < 1.0f) {
        impl_->modeBlendT = std::min(1.0f, impl_->modeBlendT + dt / k_modeCrossfadeSeconds);
    }
    const float tBlend = impl_->modeBlendT;

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
    ClipId strafeClip = ClipId::_Count;
    float strafeRatio = 0.0f;

    const bool crouchActive = (impl_->currentMode == Mode::Crouch);
    if (crouchActive) {
        pickCrouchLocomotion(impl_->smoothedSpeed, impl_->smoothedForwardSpeed, locoA, locoB, locoBlend);
        strafeClip = pickCrouchStrafeClip(impl_->smoothedRightSpeed);
    } else {
        pickLocomotion(impl_->smoothedSpeed, impl_->smoothedForwardSpeed, locoA, locoB, locoBlend);
        strafeClip = pickStrafeClip(impl_->smoothedRightSpeed, impl_->smoothedSpeed);
    }
    strafeRatio =
        (impl_->smoothedSpeed > k_idleCutoff && strafeClip != ClipId::_Count)
            ? std::clamp(std::abs(impl_->smoothedRightSpeed) / std::max(impl_->smoothedSpeed, 1.0f), 0.0f, 1.0f)
            : 0.0f;

    const float speedScale = computeSpeedScale(impl_->smoothedSpeed);

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

    s0.id = locoA;
    s0.timeRatio = impl_->locomotionPhase;
    s0.weight = (1.0f - locoBlend) * groupLoco * (1.0f - strafeRatio);
    s0.playbackSpeed = speedScale;
    s0.active = (s0.weight > 1e-4f) && impl_->library->has(locoA);

    s1.id = locoB;
    s1.timeRatio = impl_->locomotionPhase;
    s1.weight = locoBlend * groupLoco * (1.0f - strafeRatio);
    s1.playbackSpeed = speedScale;
    s1.active = (s1.weight > 1e-4f) && impl_->library->has(locoB) && (locoB != locoA);

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
    impl_->wallRunMirror =
        (impl_->currentMode == Mode::WallRun && inputs.wallRunSide == WallSideLeft) ||
        (impl_->previousMode == Mode::WallRun && tBlend < 1.0f && inputs.wallRunSide == WallSideLeft);

    // PR-29: steps 7-10 (ozz sampling → blending → local-to-model →
    // skin matrices) are factored into the lambda below so that
    // `renderFromServer()` can share the same pipeline after
    // overwriting `impl_->samplers` from a server-replicated
    // `AnimSnapshot`.  Body identical to the original lines 7-10
    // of update; only the entry point differs.
    runSamplingAndSkinning(inputs);
}

namespace
{
} // namespace

void CharacterAnimator::renderFromServer(const AnimSnapshot& serverState, const AnimationInputs& inputs)
{
    if (!impl_->rig || !impl_->rig->isLoaded() || !impl_->library)
        return;

    // Override the per-slot sampler state directly from the server's
    // snapshot.  Server's animator is authoritative for clipId,
    // timeRatio, and weight; the client renders that exact state
    // rather than re-running its own state machine and accumulating
    // drift.  `playbackSpeed` stays at the default 1.0 — server's
    // timeRatio increment already bakes in any speed multiplier on
    // its side, and we're snapping to that result.
    constexpr std::uint8_t k_inactiveClip = static_cast<std::uint8_t>(ClipId::_Count);
    for (std::size_t i = 0; i < impl_->samplers.size() && i < serverState.slots.size(); ++i) {
        const auto& src = serverState.slots[i];
        auto& dst = impl_->samplers[i];
        dst.active = (src.weight > 0.0f) && (src.clipIdRaw < k_inactiveClip);
        dst.id = dst.active ? static_cast<ClipId>(src.clipIdRaw) : ClipId::Idle;
        dst.timeRatio = src.timeRatio;
        dst.weight = src.weight;
    }

    // Wallrun mirror: server's PlayerVisState carries `wallRunSide`
    // (replicated, interp-delayed by PR-28).  When wallrun-on-left,
    // we mirror the pose along X.  Simpler than update()'s
    // mode-transition logic since the server has already picked the
    // right clip set in its samplers.
    impl_->wallRunMirror = (inputs.wallRunSide == WallSideLeft);

    // Run the shared ozz sampling + blending + skinning pipeline.
    runSamplingAndSkinning(inputs);
}

void CharacterAnimator::runSamplingAndSkinning(const AnimationInputs& inputs)
{
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
        blend.threshold = 0.01f; // Lower than default (0.1) for smoother fade-in/out.
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

    // --- 9. LocalToModelJob. ---
    ozz::animation::LocalToModelJob l2m;
    l2m.skeleton = impl_->rig->skeleton();
    l2m.input = ozz::make_span(impl_->blendedLocals);
    l2m.output = ozz::make_span(impl_->models);
    if (!l2m.Run()) {
        SDL_Log("CharacterAnimator: local-to-model failed — skin matrices not updated");
        return;
    }

    // --- 10. Compose final skin matrices. ---
    //
    // Two optional post-processing transforms:
    //   (a) Procedural head-look: rotates the head (and children) around its
    //       local X axis so the character looks up/down matching the camera pitch.
    //   (b) Wallrun mirror: scale(-1,1,1) to flip the pose for left-wall runs.

    const int nJoints = impl_->rig->numJoints();
    const auto& ibm = impl_->rig->inverseBindMatrices();

    // (a) Head pitch transform.
    glm::mat4 headPitchTransform(1.0f);
    bool hasHeadPitch = false;
    if (impl_->headJointIdx >= 0 && std::abs(inputs.pitchRad) > 0.001f) {
        const glm::mat4 headModel = anim_utils::ozzToGlm(impl_->models[static_cast<size_t>(impl_->headJointIdx)]);
        const glm::vec3 headPos(headModel[3]);
        const glm::vec3 headX = glm::normalize(glm::vec3(headModel[0]));

        const float headPitch = std::clamp(inputs.pitchRad, -k_headPitchMax, k_headPitchMax);
        headPitchTransform = glm::translate(glm::mat4(1.0f), headPos) * glm::rotate(glm::mat4(1.0f), headPitch, headX) *
                             glm::translate(glm::mat4(1.0f), -headPos);
        hasHeadPitch = true;
    }

    // (b) Wallrun mirror.
    glm::mat4 mirrorMat(1.0f);
    if (impl_->wallRunMirror)
        mirrorMat[0][0] = -1.0f;

    for (int j = 0; j < nJoints; ++j) {
        const size_t uj = static_cast<size_t>(j);
        glm::mat4 modelMat = anim_utils::ozzToGlm(impl_->models[uj]);

        if (hasHeadPitch && uj < impl_->isHeadDescendant.size() && impl_->isHeadDescendant[uj])
            modelMat = headPitchTransform * modelMat;

        // Store model-space matrix with procedural xforms (for hitbox capsule placement).
        impl_->jointModelMats[uj] = mirrorMat * modelMat;
        // Skin matrix additionally includes inverse bind matrix.
        impl_->skinMats[uj] = mirrorMat * modelMat * ibm[uj];
    }

    (void)inputs.sprinting;
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
