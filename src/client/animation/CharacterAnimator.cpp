/// @file CharacterAnimator.cpp
/// @brief Per-entity animator: state machine + sampling + blending + skin matrices.

#include "CharacterAnimator.hpp"

#include "AnimationLocomotion.hpp"

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
#include <array>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <initializer_list>
#include <limits>
#include <vector>

namespace
{

constexpr float k_accelLowPassTau = 0.045f;     ///< Responsive velocity catch-up while accelerating (s).
constexpr float k_decelLowPassTau = 0.065f;     ///< Slightly softer decay when releasing movement input (s).
constexpr float k_dirLowPassTau = 0.055f;       ///< Directional velocity smoothing for normal steering (s).
constexpr float k_dirFlipLowPassTau = 0.035f;   ///< Faster catch-up for hard left/right or forward/back flips (s).
constexpr float k_modeCrossfadeSeconds = 0.15f; ///< Slide/WallRun ↔ locomotion crossfade (s).
constexpr float k_spinePitchMax = 1.5708f;      ///< Max total spine pitch magnitude (~90 degrees).

/// Number of bones in the spine pitch chain: Spine, Spine1, Spine2, Neck, Head.
static constexpr size_t kSpineChainLength = 5;

/// Slot layout (kNumSamplerSlots = 5):
///  [0] loco primary     (Idle / Walk / Run / RunBackward)
///  [1] loco secondary   (Walk / Run during 1-D speed band blend)
///  [2] loco strafe      (StrafeLeft / StrafeRight / walk variants)
///  [3] override         (Slide / WallRun / Jump / debug clip)
///  [4] transition       (short start / stop / pivot overlays)
constexpr size_t k_slotLocoA = 0;
constexpr size_t k_slotLocoB = 1;
constexpr size_t k_slotStrafe = 2;
constexpr size_t k_slotOverride = 3;
constexpr size_t k_slotTransition = 4;

enum class Mode : uint8_t
{
    Locomotion,
    Crouch,
    Airborne, ///< In the air (not wallrunning/sliding).
    Slide,
    WallRun,
    HoldPose,
    DebugOverride,
    Emote, ///< Full-body emote (dance/taunt) forced via AnimationInputs::emoteClip.
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

/// @brief Forward-kinematics chain for one arm (weapon-hold rewrite).
///
/// Holds the bone indices, descendant masks, and rest-pose local rotations for
/// the arm-chain bones (root → tip: Shoulder, UpperArm, ForeArm, Hand) plus the
/// five finger sub-chains. `applyWeaponHoldPose` drives each bone to an authored
/// local rotation; no IK is solved.
struct ArmChain
{
    std::array<int, kArmHoldBoneCount> bones{-1, -1, -1, -1};
    std::array<std::vector<bool>, kArmHoldBoneCount> descendants;
    // Rest-pose local rotation per arm-chain bone — the authored (pitch, yaw)
    // swing is applied relative to this, so (0,0) reproduces the bind pose.
    std::array<glm::quat, kArmHoldBoneCount> restLocal{};

    struct FingerChain
    {
        std::array<int, 4> joints{-1, -1, -1, -1};
        std::array<std::vector<bool>, 4> descendants;

        [[nodiscard]] bool valid() const noexcept
        {
            return joints[0] >= 0 && joints[1] >= 0 && joints[2] >= 0 && joints[3] >= 0 && !descendants[0].empty() &&
                   !descendants[1].empty() && !descendants[2].empty();
        }
    };
    std::array<FingerChain, kHandFingerIkCount> fingers{};

    [[nodiscard]] bool valid() const noexcept
    {
        return bones[0] >= 0 && bones[1] >= 0 && bones[2] >= 0 && bones[3] >= 0 && !descendants[0].empty() &&
               !descendants[1].empty() && !descendants[2].empty() && !descendants[3].empty();
    }
};

/// @brief Procedural spine pitch chain — distributes aim pitch across spine bones.
///
/// Order: Spine (lowest) → Spine1 → Spine2 → Neck → Head (highest). Weights must
/// sum to ≤ 1.0; total pitch is `pitchRad * weights[i]` per bone, cascaded.
/// Built once at animator construction; bones[] entries are -1 if a joint was
/// not present in the rig (the chain stays usable as long as the partial
/// sequence yields a non-zero total weight).
struct SpineBendChain
{
    std::array<int, kSpineChainLength> bones{-1, -1, -1, -1, -1};
    std::array<std::vector<bool>, kSpineChainLength> descendants;
    std::array<float, kSpineChainLength> weights{0.10f, 0.25f, 0.35f, 0.20f, 0.10f};

    [[nodiscard]] bool valid() const noexcept
    {
        for (size_t i = 0; i < kSpineChainLength; ++i) {
            if (bones[i] >= 0 && !descendants[i].empty())
                return true;
        }
        return false;
    }
};

glm::vec3 matrixTranslation(const glm::mat4& m)
{
    return glm::vec3(m[3]);
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

/// @brief Extract a joint's rest-pose local rotation quaternion from the skeleton.
///
/// ozz stores rest poses as `SoaTransform` (SoaQuaternion etc.) — 4 joints per
/// SoA slot, with separate {x, y, z, w} simd-float-4 lanes. To pull a single
/// joint's quaternion we index by `j / 4` for the slot and `j % 4` for the lane,
/// then read each lane component via the standard ozz `GetByIndex` extractor.
glm::quat extractRestLocalRotation(const ozz::animation::Skeleton* skeleton, int jointIdx)
{
    if (skeleton == nullptr || jointIdx < 0)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const auto rest = skeleton->joint_rest_poses();
    const size_t soaIdx = static_cast<size_t>(jointIdx / 4);
    const int lane = jointIdx % 4;
    if (soaIdx >= rest.size())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    // Each component is a simd-float-4 packed across 4 joints; spill to a
    // float[4] and read the lane. ozz::math::Store4PtrU expects a 16-byte
    // aligned buffer for the unaligned-store path, so we declare it explicitly.
    alignas(16) float x[4];
    alignas(16) float y[4];
    alignas(16) float z[4];
    alignas(16) float w[4];
    ozz::math::StorePtrU(rest[soaIdx].rotation.x, x);
    ozz::math::StorePtrU(rest[soaIdx].rotation.y, y);
    ozz::math::StorePtrU(rest[soaIdx].rotation.z, z);
    ozz::math::StorePtrU(rest[soaIdx].rotation.w, w);
    return glm::normalize(glm::quat(w[lane], x[lane], y[lane], z[lane]));
}

/// @brief Convert a per-bone (pitch, yaw, roll) triple to a local-space quaternion.
///
/// `pitchDeg` rotates around the bone's local Z axis (Mixamo "curl" axis),
/// `yawDeg` around the local Y axis ("splay"), and `rollDeg` around the local X
/// axis (twist along the bone's length). Composition order is
/// `qYaw * qPitch * qRoll`, so with `rollDeg == 0` this reduces exactly to the
/// previous 2-DOF `qYaw * qPitch` — existing authored finger curl values stay
/// valid. The roll DOF was added so arm/hand bones can be oriented accurately
/// (e.g. pronating the forearm) which pure pitch+yaw cannot express.
inline glm::quat boneLocalQuat(float pitchDeg, float yawDeg, float rollDeg)
{
    const glm::quat qPitch = glm::angleAxis(glm::radians(pitchDeg), glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::quat qYaw = glm::angleAxis(glm::radians(yawDeg), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat qRoll = glm::angleAxis(glm::radians(rollDeg), glm::vec3{1.0f, 0.0f, 0.0f});
    return glm::normalize(qYaw * qPitch * qRoll);
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

    // Short locomotion transitions. Slot 4 is blended on top of the continuous
    // locomotion tree for starts, stops, and hard pivots. Missing authored clips
    // fall back to existing clips at reduced weight, so the graph stays smooth
    // while art catches up.
    anim_locomotion::TransitionTracker transitionTracker;
    anim_locomotion::TransitionIntent activeTransition;
    ClipId activeTransitionClip = ClipId::_Count;
    float transitionElapsedSec = 0.0f;

    // Wallrun mirror state — true when the wallrun animation needs to be
    // mirrored in X so the character leans toward the correct wall side.
    bool wallRunMirror = false;

    // Procedural spine pitch chain. Distributes aim pitch across spine bones
    // (Spine → Spine1 → Spine2 → Neck → Head) instead of rotating the head alone.
    // Runs in runSamplingAndSkinning, so it executes server-side too — hitbox
    // capsules for spine joints stay consistent with the visible pose.
    SpineBendChain spineBend;

    // Lower-body strafe mask. true for joints in the leg subtrees (LeftUpLeg /
    // RightUpLeg and their descendants). Used to restrict the strafe clip's
    // influence to the legs so the upper body keeps facing forward — the weapon
    // is aimed by the spine-bend overlay (model-space pitch) and would point
    // off-axis if the strafe pose were allowed to twist/lean the torso. Hips is
    // intentionally excluded: it is the rig root, so blending it would carry the
    // strafe rotation into the whole upper body and defeat the masking.
    std::vector<bool> lowerBodyMask;

    // Per-joint blend weights (SoA, num_soa_joints entries) rebuilt each frame
    // when the strafe slot is active. locoJointWeights drive slots 0/1 (loco),
    // strafeJointWeights drive slot 2 (strafe). On the upper body the strafe
    // weight is 0 and the loco weight is 1; on the legs they crossfade by
    // strafeBlend. ozz normalizes per joint, so the legs reproduce the original
    // forward/strafe crossfade while the torso stays pure forward-loco.
    std::vector<ozz::math::SimdFloat4> locoJointWeights;
    std::vector<ozz::math::SimdFloat4> strafeJointWeights;

    // Lateral-vs-total speed ratio in [0,1] used to weight the strafe mask on
    // the legs. Set by update() (smoothed) for the local player + server, and
    // recomputed from inputs by renderFromServer() for remote players.
    float strafeBlend = 0.0f;

    // Phase F polish state.
    int hipsJointIdx = -1;             ///< "mixamorig:Hips" index, drives the hip-lean coupling.
    std::vector<bool> hipsDescendants; ///< Descendant mask for the hip-lean delta.
    float recoilPitch = 0.0f;          ///< Current additive pitch from recoil (decays each frame).
    float breathingPhase = 0.0f;       ///< Accumulated time for the breathing oscillator (s, wraps every 2*pi).

    // Weapon-hold FK chains.
    ArmChain leftArm;
    ArmChain rightArm;

    // Debug override.
    ClipId debugOverrideId = ClipId::_Count;
    float debugPlaybackSpeedMul = 1.0f;

    // Active full-body emote clip (driven by AnimationInputs::emoteClip each
    // update); `_Count` means no emote.
    ClipId emoteClipId = ClipId::_Count;

    // Freeze playback flag (3P Weapon Tweaker uses this so world-space anchor
    // sliders don't drift from the idle bob). When true, update() and
    // renderFromServer() skip everything except runSamplingAndSkinning, which
    // re-derives the pose from the last-known sampler timeRatios — so the
    // base pose is identical every frame and IK can mutate it cleanly.
    bool frozen = false;

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

    // Cache spine chain bone indices and build descendant masks for procedural
    // pitch distribution. Replaces the old head-only pitch — head is now the
    // last entry in the spine chain.
    if (rig.isLoaded() && rig.skeleton()) {
        const auto& jm = rig.jointMap();

        static constexpr std::array<const char*, kSpineChainLength> k_spineBoneNames = {
            "mixamorig:Spine",
            "mixamorig:Spine1",
            "mixamorig:Spine2",
            "mixamorig:Neck",
            "mixamorig:Head",
        };
        int foundBones = 0;
        for (size_t i = 0; i < kSpineChainLength; ++i) {
            const auto it = jm.find(k_spineBoneNames[i]);
            if (it == jm.end())
                continue;
            impl_->spineBend.bones[i] = it->second;
            impl_->spineBend.descendants[i] = buildDescendantMask(rig.skeleton(), it->second);
            ++foundBones;
        }

        // Phase F hip-lean: a small counter-pitch applied to the hips (and
        // everything below — Hips is the root of the rig, so the mask is
        // mostly used for completeness/parity with the spine bend code path).
        if (const auto hipsIt = jm.find("mixamorig:Hips"); hipsIt != jm.end()) {
            impl_->hipsJointIdx = hipsIt->second;
            impl_->hipsDescendants = buildDescendantMask(rig.skeleton(), impl_->hipsJointIdx);
        }

        // Lower-body strafe mask: the union of the two leg subtrees. Restricts
        // the strafe clip to the legs so the torso/arms keep their forward
        // locomotion orientation and the weapon stays on-aim.
        {
            impl_->lowerBodyMask.assign(static_cast<size_t>(numJoints), false);
            int legRoots = 0;
            for (const char* legRootName : {"mixamorig:LeftUpLeg", "mixamorig:RightUpLeg"}) {
                const auto it = jm.find(legRootName);
                if (it == jm.end())
                    continue;
                const std::vector<bool> sub = buildDescendantMask(rig.skeleton(), it->second);
                const size_t n = std::min(sub.size(), impl_->lowerBodyMask.size());
                for (size_t j = 0; j < n; ++j)
                    impl_->lowerBodyMask[j] = impl_->lowerBodyMask[j] || sub[j];
                ++legRoots;
            }
            if (legRoots > 0) {
                const int numSoa = rig.skeleton() ? rig.skeleton()->num_soa_joints() : 0;
                impl_->locoJointWeights.resize(static_cast<size_t>(numSoa));
                impl_->strafeJointWeights.resize(static_cast<size_t>(numSoa));
                SDL_Log("CharacterAnimator: strafe lower-body mask bound (%d/2 leg roots)", legRoots);
            } else {
                impl_->lowerBodyMask.clear(); // No legs found — disables masking gracefully.
            }
        }

        if (foundBones > 0) {
            SDL_Log("CharacterAnimator: spine bend chain bound (%d/%d bones found)",
                    foundBones,
                    static_cast<int>(kSpineChainLength));
        }

        auto makeFingerChain = [&](const char* prefix) {
            ArmChain::FingerChain finger;
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
            ArmChain chain;
            const std::string sidePrefix = std::string("mixamorig:") + side;
            // Arm-chain bones root -> tip: Shoulder, UpperArm (Arm), ForeArm, Hand.
            bool allFound = true;
            for (std::size_t i = 0; i < kArmHoldBoneCount; ++i) {
                const auto it = jm.find(sidePrefix + kArmHoldBoneSuffixes[i]);
                if (it == jm.end()) {
                    allFound = false;
                    break;
                }
                chain.bones[i] = it->second;
                chain.descendants[i] = buildDescendantMask(rig.skeleton(), it->second);
                chain.restLocal[i] = extractRestLocalRotation(rig.skeleton(), it->second);
            }
            if (!allFound) {
                chain = ArmChain{};
                return chain;
            }
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

void CharacterAnimator::applyRecoilImpulse(float strengthRad)
{
    // Recoil kicks are intentionally "looking up" — negative pitch in our
    // convention (positive = looking down) — so callers pass a positive
    // magnitude and we set the impulse to its negation. Multiple shots in
    // quick succession stack until the decay catches up.
    impl_->recoilPitch -= std::max(0.0f, strengthRad);
}

void CharacterAnimator::applyWeaponHoldPose(const WeaponHoldPose& pose, float weight)
{
    if (!impl_->rig || impl_->jointModelMats.empty() || impl_->skinMats.empty())
        return;
    const float w = std::clamp(weight, 0.0f, 1.0f);
    if (w <= 0.0f)
        return;

    const auto parents = impl_->rig->skeleton()->joint_parents();
    auto& mats = impl_->jointModelMats;

    // Drive one bone to a target LOCAL rotation, slerped from the sampled
    // animation by `w`, applied as a model-space delta that cascades over the
    // bone's whole subtree (preserving bone lengths — same trick the spine bend
    // and the old grip pose used). Bones MUST be processed parent → child so
    // each child reads its parent's already-updated frame: that's what makes
    // this true forward kinematics rather than independent per-bone nudges.
    auto applyLocal = [&](int boneIdx, const std::vector<bool>& descendants, const glm::quat& targetLocal) {
        if (boneIdx < 0 || descendants.empty())
            return;
        const int parentIdx = static_cast<int>(parents[static_cast<size_t>(boneIdx)]);
        const glm::mat4& boneMat = mats[static_cast<size_t>(boneIdx)];
        const glm::mat3 boneR(glm::normalize(glm::vec3(boneMat[0])),
                              glm::normalize(glm::vec3(boneMat[1])),
                              glm::normalize(glm::vec3(boneMat[2])));
        glm::mat3 parentR(1.0f);
        if (parentIdx >= 0) {
            const glm::mat4& pMat = mats[static_cast<size_t>(parentIdx)];
            parentR = glm::mat3(glm::normalize(glm::vec3(pMat[0])),
                                glm::normalize(glm::vec3(pMat[1])),
                                glm::normalize(glm::vec3(pMat[2])));
        }
        const glm::quat animatedLocal = glm::normalize(glm::quat_cast(glm::transpose(parentR) * boneR));
        const glm::quat blendedLocal = glm::normalize(glm::slerp(animatedLocal, glm::normalize(targetLocal), w));
        const glm::quat deltaRot =
            glm::normalize(glm::quat_cast(parentR * glm::mat3_cast(blendedLocal) * glm::transpose(boneR)));
        const glm::vec3 pivot = matrixTranslation(boneMat);
        applyDeltaToMask(mats, descendants, rotateAround(pivot, deltaRot));
    };

    auto poseArm = [&](const ArmChain& chain, const ArmHoldPose& arm) {
        if (!chain.valid())
            return;
        // Arm-chain bones root → tip: authored (pitch, yaw, roll) applied
        // relative to the bone's rest local rotation, so (0,0,0) = bind pose.
        for (std::size_t i = 0; i < kArmHoldBoneCount; ++i) {
            const glm::vec3 a = arm.boneAngles[i];
            const glm::quat targetLocal = glm::normalize(chain.restLocal[i] * boneLocalQuat(a.x, a.y, a.z));
            applyLocal(chain.bones[i], chain.descendants[i], targetLocal);
        }
        // Fingers: authored (pitch, yaw, roll) as an absolute local rotation —
        // with roll = 0 this matches the previous GripPose convention so
        // hand-tuned curl values carry over.
        for (std::size_t finger = 0; finger < kHandFingerIkCount; ++finger) {
            const ArmChain::FingerChain& fc = chain.fingers[finger];
            if (!fc.valid())
                continue;
            for (std::size_t j = 0; j < kGripPoseBonesPerFinger; ++j) {
                const glm::vec3 a = arm.fingerAngles[GripPose::index(finger, j)];
                applyLocal(fc.joints[j], fc.descendants[j], boneLocalQuat(a.x, a.y, a.z));
            }
        }
    };

    poseArm(impl_->rightArm, pose.rightArm);
    poseArm(impl_->leftArm, pose.leftArm);

    const std::vector<glm::mat4>& inverseBind = impl_->rig->inverseBindMatrices();
    const size_t count = std::min(impl_->skinMats.size(), inverseBind.size());
    for (size_t i = 0; i < count; ++i)
        impl_->skinMats[i] = impl_->jointModelMats[i] * inverseBind[i];
}

void CharacterAnimator::updateSkinMatrices()
{
    if (!impl_->rig || impl_->skinMats.empty())
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

int CharacterAnimator::currentModeValue() const noexcept
{
    return static_cast<int>(impl_->currentMode);
}

void CharacterAnimator::setFrozen(bool frozen) noexcept
{
    impl_->frozen = frozen;
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

ClipId firstLoaded(const AnimationLibrary& lib, std::initializer_list<ClipId> clips)
{
    for (ClipId clip : clips) {
        if (clip != ClipId::_Count && lib.has(clip))
            return clip;
    }
    return ClipId::_Count;
}

ClipId resolveLocomotionClip(const AnimationLibrary& lib, ClipId requested)
{
    if (requested == ClipId::_Count || lib.has(requested))
        return requested;

    switch (requested) {
    case ClipId::CrouchWalk:
        return firstLoaded(lib, {ClipId::CrouchWalkLeft, ClipId::CrouchWalkRight, ClipId::Walk, ClipId::Idle});
    case ClipId::CrouchWalkBackward:
        return firstLoaded(lib, {ClipId::RunBackward, ClipId::CrouchWalkRight, ClipId::Walk, ClipId::Idle});
    case ClipId::CrouchWalkLeft:
    case ClipId::CrouchWalkRight:
        return firstLoaded(lib, {ClipId::CrouchWalk, ClipId::CrouchIdle});
    case ClipId::StrafeLeftWalk:
        return firstLoaded(lib, {ClipId::StrafeLeft});
    case ClipId::StrafeRightWalk:
        return firstLoaded(lib, {ClipId::StrafeRight});
    case ClipId::StrafeLeft:
        return firstLoaded(lib, {ClipId::StrafeLeftWalk});
    case ClipId::StrafeRight:
        return firstLoaded(lib, {ClipId::StrafeRightWalk});
    case ClipId::Walk:
        return firstLoaded(lib, {ClipId::Run, ClipId::SlowRun, ClipId::Idle});
    case ClipId::Run:
        return firstLoaded(lib, {ClipId::SlowRun, ClipId::Walk, ClipId::Idle});
    case ClipId::RunBackward:
        return firstLoaded(lib, {ClipId::Walk, ClipId::Idle});
    case ClipId::CrouchIdle:
        return firstLoaded(lib, {ClipId::Idle});
    default:
        return ClipId::_Count;
    }
}

ClipId resolveTransitionClip(const AnimationLibrary& lib, const anim_locomotion::TransitionIntent& intent)
{
    if (intent.kind == anim_locomotion::TransitionKind::None)
        return ClipId::_Count;
    if (intent.preferredClip != ClipId::_Count && lib.has(intent.preferredClip))
        return intent.preferredClip;

    const ClipId fallback = anim_locomotion::fallbackTransitionClip(intent.kind, intent.preferredClip);
    if (fallback != ClipId::_Count && lib.has(fallback))
        return fallback;
    return ClipId::_Count;
}

} // namespace

void CharacterAnimator::update(const AnimationInputs& inputs, float dt)
{
    if (!impl_->rig || !impl_->rig->isLoaded() || !impl_->library)
        return;

    // Freeze toggle (debug authoring): re-sample the same pose every frame
    // without advancing any internal time/state. The existing sampler entries
    // (chosen on the last unfrozen frame) keep their timeRatios; we just
    // re-run the ozz pipeline + procedural overlays so the IK pass has a
    // clean base pose to mutate. Spine bend still picks up live inputs.pitchRad
    // so the camera aim stays responsive while the body locomotion freezes.
    if (impl_->frozen) {
        runSamplingAndSkinning(inputs);
        return;
    }

    // --- 1. Smooth local-space velocity. ---
    const anim_locomotion::LocalVelocity rawLocal =
        anim_locomotion::localVelocityFromWorld(inputs.velocityWorld, inputs.yawRad);
    const float rawSpeed = anim_locomotion::speed(rawLocal);

    const auto smoothAxis = [dt](float current, float target) {
        const bool signFlip = current * target < 0.0f;
        const float tau = signFlip ? k_dirFlipLowPassTau : k_dirLowPassTau;
        const float alpha = anim_locomotion::smoothingAlpha(dt, tau);
        return current + (target - current) * alpha;
    };
    impl_->smoothedForwardSpeed = smoothAxis(impl_->smoothedForwardSpeed, rawLocal.forward);
    impl_->smoothedRightSpeed = smoothAxis(impl_->smoothedRightSpeed, rawLocal.right);

    const float smoothedLocalSpeed =
        anim_locomotion::speed({.forward = impl_->smoothedForwardSpeed, .right = impl_->smoothedRightSpeed});
    const float speedTau = rawSpeed >= impl_->smoothedSpeed ? k_accelLowPassTau : k_decelLowPassTau;
    const float speedAlpha = anim_locomotion::smoothingAlpha(dt, speedTau);
    impl_->smoothedSpeed += (smoothedLocalSpeed - impl_->smoothedSpeed) * speedAlpha;
    if (rawSpeed < anim_locomotion::k_idleCutoff && impl_->smoothedSpeed < 1.0f) {
        impl_->smoothedSpeed = 0.0f;
        impl_->smoothedForwardSpeed = 0.0f;
        impl_->smoothedRightSpeed = 0.0f;
    }

    // --- 2. Determine target mode. ---
    // Resolve the requested emote clip (if any) from the per-frame input.
    impl_->emoteClipId = (inputs.emoteClip >= 0 && inputs.emoteClip < static_cast<int>(ClipId::_Count))
                             ? static_cast<ClipId>(inputs.emoteClip)
                             : ClipId::_Count;

    Mode targetMode = Mode::Locomotion;
    if (impl_->debugOverrideId != ClipId::_Count) {
        targetMode = Mode::DebugOverride;
    } else if (impl_->emoteClipId != ClipId::_Count) {
        targetMode = Mode::Emote;
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
            targetMode == Mode::DebugOverride || targetMode == Mode::Emote)
            impl_->overrideTime = 0.0f;
    }
    if (impl_->modeBlendT < 1.0f) {
        impl_->modeBlendT = std::min(1.0f, impl_->modeBlendT + dt / k_modeCrossfadeSeconds);
    }
    const float tBlend = impl_->modeBlendT;

    const auto overrideWeightFor = [](Mode m) -> float {
        return (m == Mode::Slide || m == Mode::WallRun || m == Mode::Airborne || m == Mode::DebugOverride ||
                m == Mode::Emote)
                   ? 1.0f
                   : 0.0f;
    };
    const float prevOverride = overrideWeightFor(impl_->previousMode);
    const float targetOverride = overrideWeightFor(impl_->currentMode);
    impl_->groupWeightOverride = prevOverride + (targetOverride - prevOverride) * tBlend;
    const float groupLoco = 1.0f - impl_->groupWeightOverride;

    // --- 4. Locomotion slots (always computed for phase continuity). ---
    const bool crouchActive = (impl_->currentMode == Mode::Crouch);
    const anim_locomotion::LocalVelocity smoothedLocal{
        .forward = impl_->smoothedForwardSpeed,
        .right = impl_->smoothedRightSpeed,
    };
    const anim_locomotion::LocomotionSelection requestedLoco =
        anim_locomotion::selectLocomotion(smoothedLocal, crouchActive);

    const auto applyClipFallback = [this](ClipId requested) {
        const ClipId resolved = resolveLocomotionClip(*impl_->library, requested);
        if (requested != ClipId::_Count && resolved != requested &&
            !impl_->missingClipLogged[static_cast<size_t>(requested)])
        {
            SDL_Log("CharacterAnimator: clip '%s' not loaded — using '%s' fallback",
                    clipName(requested),
                    resolved == ClipId::_Count ? "(none)" : clipName(resolved));
            impl_->missingClipLogged[static_cast<size_t>(requested)] = true;
        }
        return resolved;
    };

    ClipId locoA = applyClipFallback(requestedLoco.primary);
    ClipId locoB = applyClipFallback(requestedLoco.secondary);
    ClipId strafeClip = applyClipFallback(requestedLoco.strafeClip);
    float locoBlend = requestedLoco.secondaryWeight;
    if (locoA == ClipId::_Count) {
        locoA = ClipId::Idle;
        locoBlend = 0.0f;
    }
    if (locoB == ClipId::_Count)
        locoB = locoA;
    if (locoB == locoA)
        locoBlend = 0.0f;

    const float strafeRatio = (strafeClip != ClipId::_Count) ? requestedLoco.strafeBlend : 0.0f;
    const float speedScale = requestedLoco.speedScale;
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
    auto& sTransition = impl_->samplers[k_slotTransition];

    // Strafe is applied as a per-joint blend on the legs only (see
    // runSamplingAndSkinning). The forward/backward locomotion clips therefore
    // keep their FULL group weight here — they must stay sampled at non-zero
    // weight even during pure lateral movement so the upper body has a
    // forward-facing pose to fall back on. The strafe slot also carries the
    // full group weight; its actual influence is scaled per joint by the
    // lower-body mask × strafeBlend at blend time. (Previously these scalar
    // weights folded in (1 - strafeRatio) / strafeRatio, which faded the
    // forward clips to zero during pure strafe and let the strafe pose twist
    // the whole torso off-aim.)
    impl_->strafeBlend = strafeRatio;

    s0.id = locoA;
    s0.timeRatio = impl_->locomotionPhase;
    s0.weight = (1.0f - locoBlend) * groupLoco;
    s0.playbackSpeed = speedScale;
    s0.active = (s0.weight > 1e-4f) && impl_->library->has(locoA);

    s1.id = locoB;
    s1.timeRatio = impl_->locomotionPhase;
    s1.weight = locoBlend * groupLoco;
    s1.playbackSpeed = speedScale;
    s1.active = (s1.weight > 1e-4f) && impl_->library->has(locoB) && (locoB != locoA);

    sStrafe.id = strafeClip;
    sStrafe.timeRatio = impl_->locomotionPhase;
    sStrafe.weight = groupLoco;
    sStrafe.playbackSpeed = speedScale;
    sStrafe.active = (strafeRatio > 1e-4f) && (groupLoco > 1e-4f) && (strafeClip != ClipId::_Count) &&
                     impl_->library->has(strafeClip);

    const anim_locomotion::TransitionIntent transitionIntent =
        (groupLoco > 1e-4f) ? anim_locomotion::updateTransitionTracker(impl_->transitionTracker, rawLocal, dt)
                            : anim_locomotion::TransitionIntent{};
    if (transitionIntent.kind != anim_locomotion::TransitionKind::None) {
        impl_->activeTransition = transitionIntent;
        impl_->activeTransitionClip = resolveTransitionClip(*impl_->library, transitionIntent);
        impl_->transitionElapsedSec = 0.0f;
        if (transitionIntent.preferredClip != ClipId::_Count &&
            impl_->activeTransitionClip != transitionIntent.preferredClip &&
            !impl_->missingClipLogged[static_cast<size_t>(transitionIntent.preferredClip)])
        {
            SDL_Log("CharacterAnimator: transition clip '%s' not loaded — using '%s' fallback",
                    clipName(transitionIntent.preferredClip),
                    impl_->activeTransitionClip == ClipId::_Count ? "(none)" : clipName(impl_->activeTransitionClip));
            impl_->missingClipLogged[static_cast<size_t>(transitionIntent.preferredClip)] = true;
        }
    }

    sTransition.active = false;
    sTransition.weight = 0.0f;
    if (impl_->activeTransition.kind != anim_locomotion::TransitionKind::None) {
        impl_->transitionElapsedSec += dt;
        const float weight = anim_locomotion::transitionWeight(impl_->activeTransition.kind,
                                                               impl_->transitionElapsedSec,
                                                               impl_->activeTransition.durationSec,
                                                               impl_->activeTransition.peakWeight) *
                             groupLoco;
        if (impl_->activeTransitionClip != ClipId::_Count && weight > 1e-4f) {
            sTransition.id = impl_->activeTransitionClip;
            sTransition.timeRatio = anim_locomotion::transitionPlaybackRatio(impl_->transitionElapsedSec,
                                                                             impl_->activeTransition.durationSec);
            sTransition.weight = weight;
            sTransition.playbackSpeed = 1.0f;
            sTransition.active = impl_->library->has(impl_->activeTransitionClip);
        }
        if (impl_->transitionElapsedSec >= impl_->activeTransition.durationSec || groupLoco <= 1e-4f) {
            impl_->activeTransition = {};
            impl_->activeTransitionClip = ClipId::_Count;
            impl_->transitionElapsedSec = 0.0f;
        }
    }

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
    case Mode::Emote:
        overrideClip = impl_->emoteClipId;
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
        case Mode::Emote:
            overrideClip = impl_->emoteClipId;
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

    // Freeze toggle: ignore the incoming snapshot, replay the last known
    // sampler state. Procedural overlays still run so the gun follows aim
    // pitch while the body itself stays static — matches the behaviour of
    // update()'s freeze path above so toggling on/off looks the same on
    // local and remote characters.
    if (impl_->frozen) {
        runSamplingAndSkinning(inputs);
        return;
    }

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

    // Recompute the lower-body strafe blend from the (interp-delayed) inputs.
    // The snapshot carries each slot's full group weight but not strafeRatio,
    // so the leg mask is reconstructed here. Upper-body correctness does not
    // depend on the exact value — the mask zeroes the strafe layer on the torso
    // unconditionally — so an instantaneous (unsmoothed) estimate is fine.
    {
        const anim_locomotion::LocalVelocity local =
            anim_locomotion::localVelocityFromWorld(inputs.velocityWorld, inputs.yawRad);
        const float speed = anim_locomotion::speed(local);
        if (speed > anim_locomotion::k_idleCutoff) {
            impl_->strafeBlend = std::clamp(std::abs(local.right) / std::max(speed, 1.0f), 0.0f, 1.0f);
        } else {
            impl_->strafeBlend = 0.0f;
        }
    }

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
        // Partial-blend mask: when the strafe slot is active, restrict it to the
        // leg subtrees and keep the locomotion slots at full weight on the upper
        // body. This must be coupled with the strafe slot's "full group weight"
        // scheme in update()/renderFromServer() — the strafe layer's real
        // influence lives entirely in these per-joint weights, so it must never
        // blend without them. On the upper body: strafe weight 0, loco weight 1
        // (pure forward-facing torso → weapon stays on-aim). On the legs: strafe
        // weight strafeBlend, loco weight (1 - strafeBlend) → original crossfade.
        const bool maskStrafe =
            impl_->samplers[k_slotStrafe].active && !impl_->lowerBodyMask.empty() && !impl_->locoJointWeights.empty();
        if (maskStrafe) {
            const float sb = std::clamp(impl_->strafeBlend, 0.0f, 1.0f);
            const size_t numSoa = impl_->locoJointWeights.size();
            const size_t numJoints = impl_->lowerBodyMask.size();
            for (size_t g = 0; g < numSoa; ++g) {
                float lLoco[4];
                float lStrafe[4];
                for (int lane = 0; lane < 4; ++lane) {
                    const size_t j = g * 4 + static_cast<size_t>(lane);
                    const bool lower = (j < numJoints) && impl_->lowerBodyMask[j];
                    lLoco[lane] = lower ? (1.0f - sb) : 1.0f;
                    lStrafe[lane] = lower ? sb : 0.0f;
                }
                impl_->locoJointWeights[g] = ozz::math::simd_float4::Load(lLoco[0], lLoco[1], lLoco[2], lLoco[3]);
                impl_->strafeJointWeights[g] =
                    ozz::math::simd_float4::Load(lStrafe[0], lStrafe[1], lStrafe[2], lStrafe[3]);
            }
        }

        std::array<ozz::animation::BlendingJob::Layer, kNumSamplerSlots> layers;
        int layerCount = 0;
        for (size_t i = 0; i < impl_->samplers.size(); ++i) {
            const auto& samp = impl_->samplers[i];
            if (!samp.active)
                continue;
            auto& layer = layers[static_cast<size_t>(layerCount)];
            layer.weight = samp.weight;
            layer.transform = ozz::make_span(impl_->perSamplerLocals[i]);
            if (maskStrafe) {
                if (i == k_slotLocoA || i == k_slotLocoB)
                    layer.joint_weights = ozz::make_span(impl_->locoJointWeights);
                else if (i == k_slotStrafe)
                    layer.joint_weights = ozz::make_span(impl_->strafeJointWeights);
            }
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
    //   (a) Procedural spine bend: distributes aim pitch across the spine
    //       chain (Spine → Spine1 → Spine2 → Neck → Head) by authored weights
    //       so the upper body leans into the aim direction. Rotation axis is
    //       the rig's model-space X (a single, fixed axis) — using each bone's
    //       local X would accumulate twist as bends compose up the chain.
    //   (b) Wallrun mirror: scale(-1,1,1) to flip the pose for left-wall runs.
    //
    // Order matters: spine bend operates in the un-mirrored model space first,
    // then the mirror is applied on top. The mirror flips the X axis globally,
    // which would otherwise flip the spine-bend rotation direction; keeping the
    // mirror as the final left-multiplication preserves pitch sign on both walls.

    const int nJoints = impl_->rig->numJoints();
    const auto& ibm = impl_->rig->inverseBindMatrices();

    // Step 1: Convert ozz model matrices to glm.
    for (int j = 0; j < nJoints; ++j) {
        const size_t uj = static_cast<size_t>(j);
        impl_->jointModelMats[uj] = anim_utils::ozzToGlm(impl_->models[uj]);
    }

    // Step 2: Phase F procedural overlays + spine bend cascade.
    //
    // Order: hip-lean first (operates on the root, all other transforms
    // compose on top), then spine bend (cascades up the chain), then
    // breathing oscillation on the chest, then the additive recoil pitch
    // on the chest. The recoil decay runs unconditionally so the impulse
    // returns to zero whether or not the spine chain is bound.

    // Decay the recoil impulse exponentially. Half-life ~250 ms gives a
    // crisp kick that's mostly gone by the time a typical follow-up shot
    // fires; the spine bend code below picks this up additively.
    {
        constexpr float k_recoilHalfLifeSec = 0.25f;
        const float dt = std::max(0.0f, inputs.dtSec);
        if (dt > 0.0f && std::abs(impl_->recoilPitch) > 1e-5f) {
            const float decay = std::exp(-glm::ln_two<float>() * dt / k_recoilHalfLifeSec);
            impl_->recoilPitch *= decay;
        }
        if (std::abs(impl_->recoilPitch) < 1e-5f)
            impl_->recoilPitch = 0.0f;
    }

    // Advance the breathing oscillator.
    {
        constexpr float k_breathFreqHz = 0.4f;
        impl_->breathingPhase += std::max(0.0f, inputs.dtSec) * k_breathFreqHz * glm::two_pi<float>();
        if (impl_->breathingPhase > glm::two_pi<float>() * 16.0f)
            impl_->breathingPhase -= glm::two_pi<float>() * 16.0f;
    }
    const float breathPitch = std::sin(impl_->breathingPhase) * glm::radians(0.5f);

    // Hip lean coupling. When the camera pitches forward (positive pitchRad =
    // looking down in this codebase's convention), the pelvis tips back
    // slightly so the silhouette reads as leaning rather than folding.
    if (impl_->hipsJointIdx >= 0 && !impl_->hipsDescendants.empty() && inputs.hipLeanMultiplier != 0.0f) {
        constexpr float k_hipLeanMaxRad = 0.087266f; // 5 degrees.
        const float hipPitch =
            std::clamp(-inputs.pitchRad * inputs.hipLeanMultiplier, -k_hipLeanMaxRad, k_hipLeanMaxRad);
        if (std::abs(hipPitch) > 1e-4f) {
            const glm::vec3 axis(1.0f, 0.0f, 0.0f);
            const glm::vec3 pivot = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(impl_->hipsJointIdx)]);
            const glm::quat rot = glm::angleAxis(hipPitch, axis);
            applyDeltaToMask(impl_->jointModelMats, impl_->hipsDescendants, rotateAround(pivot, rot));
        }
    }

    // Spine bend cascade — distribute aim pitch across the spine chain,
    // scaled by the per-weapon-class multiplier (heavy weapons get less aim
    // bend so the upper body reads as harder-to-move). Recoil and breathing
    // are folded into the effective pitch — they share the same chain so
    // each spine bone gets a proportional share of all three sources.
    if (impl_->spineBend.valid()) {
        const float scaledAimPitch = inputs.pitchRad * std::max(0.0f, inputs.spineBendMultiplier);
        const float effectivePitch = scaledAimPitch + impl_->recoilPitch + breathPitch;
        const float clampedPitch = std::clamp(effectivePitch, -k_spinePitchMax, k_spinePitchMax);
        if (std::abs(clampedPitch) > 0.0001f) {
            // Rig's model-space right axis. Single fixed axis prevents twist
            // accumulation up the chain. Mixamo rigs in bind pose have X = right.
            const glm::vec3 axis(1.0f, 0.0f, 0.0f);

            for (size_t i = 0; i < kSpineChainLength; ++i) {
                const int boneIdx = impl_->spineBend.bones[i];
                if (boneIdx < 0 || impl_->spineBend.descendants[i].empty())
                    continue;
                const float angle = clampedPitch * impl_->spineBend.weights[i];
                if (std::abs(angle) < 0.0001f)
                    continue;

                const glm::vec3 pivot = matrixTranslation(impl_->jointModelMats[static_cast<size_t>(boneIdx)]);
                const glm::quat rot = glm::angleAxis(angle, axis);
                applyDeltaToMask(impl_->jointModelMats, impl_->spineBend.descendants[i], rotateAround(pivot, rot));
            }
        }
    }

    // Step 3: Wallrun mirror (applied last, as a global X flip).
    glm::mat4 mirrorMat(1.0f);
    if (impl_->wallRunMirror)
        mirrorMat[0][0] = -1.0f;

    for (int j = 0; j < nJoints; ++j) {
        const size_t uj = static_cast<size_t>(j);
        if (impl_->wallRunMirror)
            impl_->jointModelMats[uj] = mirrorMat * impl_->jointModelMats[uj];
        impl_->skinMats[uj] = impl_->jointModelMats[uj] * ibm[uj];
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

float computeIdleGroundedMinY(const CharacterRig& rig, const AnimationLibrary& library,
                              const glm::quat& /*orientationFix*/, float bindMeshMinY)
{
    if (!rig.isLoaded() || !library.has(ClipId::Idle))
        return bindMeshMinY;

    // Sample the actual displayed IDLE pose. The character never renders at the
    // T-pose bind — it always plays a clip, and Mixamo clips reposition the
    // skeleton (the hip/root sits at the clip's floor reference, not the bind
    // mesh's), which lifts the whole body off the ground. Settle a throwaway
    // animator into Idle over several frames (so the mode crossfade + override
    // time stabilise), then take the lowest *skinned* vertex — that is exactly
    // where the rendered feet land. No skinning backend is needed: skinMatrices()
    // is the LBS palette, populated by update().
    CharacterAnimator probe(rig, library);
    probe.setDebugOverride(ClipId::Idle);
    AnimationInputs in{};
    in.grounded = true;
    for (int i = 0; i < 30; ++i)
        probe.update(in, 1.0f / 30.0f);

    const std::vector<glm::mat4>& palette = probe.skinMatrices();
    if (palette.empty())
        return bindMeshMinY;

    float minY = std::numeric_limits<float>::max();
    for (const RigMeshData& mesh : rig.meshes()) {
        for (size_t v = 0; v < mesh.baseVertices.size(); ++v) {
            const glm::vec4 p(mesh.baseVertices[v].position, 1.0f);
            const SkinWeight& w = mesh.skinWeights[v];
            glm::vec3 skinned(0.0f);
            for (int k = 0; k < 4; ++k) {
                if (w.weights[k] <= 0.0f)
                    continue;
                const int b = w.boneIndices[k];
                if (b < 0 || b >= static_cast<int>(palette.size()))
                    continue;
                skinned += w.weights[k] * glm::vec3(palette[static_cast<size_t>(b)] * p);
            }
            minY = std::min(minY, skinned.y);
        }
    }

    return (minY == std::numeric_limits<float>::max()) ? bindMeshMinY : minY;
}
