/// @file AnimationLibrary.cpp
/// @brief Load FBX animation clips onto a shared skeleton.

#include "AnimationLibrary.hpp"

#include "CharacterRig.hpp"

#include <SDL3/SDL_log.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <array>
#include <unordered_map>

const char* clipName(ClipId id)
{
    switch (id) {
    case ClipId::Idle:
        return "Idle";
    case ClipId::Walk:
        return "Walk";
    case ClipId::Run:
        return "Run";
    case ClipId::RunBackward:
        return "Run Backward";
    case ClipId::SlowRun:
        return "Slow Run";
    case ClipId::Slide:
        return "Slide";
    case ClipId::WallRun:
        return "Wall Run";
    case ClipId::Jump:
        return "Jump";
    case ClipId::StrafeLeft:
        return "Strafe Left";
    case ClipId::StrafeRight:
        return "Strafe Right";
    case ClipId::StrafeLeftWalk:
        return "Strafe Left Walk";
    case ClipId::StrafeRightWalk:
        return "Strafe Right Walk";
    case ClipId::TurnLeft90:
        return "Turn Left 90";
    case ClipId::TurnRight90:
        return "Turn Right 90";
    case ClipId::CrouchIdle:
        return "Crouch Idle";
    case ClipId::CrouchWalk:
        return "Crouch Walk";
    case ClipId::CrouchWalkLeft:
        return "Crouch Walk Left";
    case ClipId::CrouchWalkRight:
        return "Crouch Walk Right";
    case ClipId::CrouchWalkBackward:
        return "Crouch Walk Backward";
    case ClipId::StartForward:
        return "Start Forward";
    case ClipId::StartBackward:
        return "Start Backward";
    case ClipId::StartLeft:
        return "Start Left";
    case ClipId::StartRight:
        return "Start Right";
    case ClipId::StopForward:
        return "Stop Forward";
    case ClipId::StopBackward:
        return "Stop Backward";
    case ClipId::StopLeft:
        return "Stop Left";
    case ClipId::StopRight:
        return "Stop Right";
    case ClipId::PivotLeft:
        return "Pivot Left";
    case ClipId::PivotRight:
        return "Pivot Right";
    case ClipId::_Count:
        return "(none)";
    }
    return "(unknown)";
}

const char* clipFile(ClipId id)
{
    // Apex light-legend (Wraith) locomotion, converted from
    // bulk_anims/animseq/humans/class/light/mp_pilot_light_core/*.cast onto
    // Wraith's own skeleton (see assets/anims_apex/). Every ClipId resolves to
    // a real Apex clip — states without a dedicated base clip (strafes, turns,
    // starts/stops/pivots) reuse the nearest motion so nothing falls back to
    // the T-pose. Dedicated strafe/transition clips are a later refinement.
    switch (id) {
    case ClipId::Idle:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::Walk:
        return "anims_apex/apex_walk_f.glb";
    case ClipId::Run:
        return "anims_apex/apex_run_f.glb";
    case ClipId::RunBackward:
        return "anims_apex/apex_run_b.glb";
    case ClipId::SlowRun:
        return "anims_apex/apex_run_f.glb";
    case ClipId::Slide:
        return "anims_apex/apex_slide.glb";
    case ClipId::WallRun:
        return "anims_apex/apex_wallrun.glb";
    case ClipId::Jump:
        return "anims_apex/apex_jump_f.glb";
    case ClipId::StrafeLeft: // no base strafe clip yet → reuse run
        return "anims_apex/apex_run_f.glb";
    case ClipId::StrafeRight:
        return "anims_apex/apex_run_f.glb";
    case ClipId::StrafeLeftWalk:
        return "anims_apex/apex_walk_f.glb";
    case ClipId::StrafeRightWalk:
        return "anims_apex/apex_walk_f.glb";
    case ClipId::TurnLeft90:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::TurnRight90:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::CrouchIdle:
        return "anims_apex/apex_idle_crouch.glb";
    case ClipId::CrouchWalk:
        return "anims_apex/apex_crouchwalk_f.glb";
    case ClipId::CrouchWalkLeft:
        return "anims_apex/apex_crouchwalk_f.glb";
    case ClipId::CrouchWalkRight:
        return "anims_apex/apex_crouchwalk_f.glb";
    case ClipId::CrouchWalkBackward:
        return "anims_apex/apex_crouchwalk_b.glb";
    case ClipId::StartForward:
        return "anims_apex/apex_walk_f.glb";
    case ClipId::StartBackward:
        return "anims_apex/apex_walk_b.glb";
    case ClipId::StartLeft:
        return "anims_apex/apex_walk_f.glb";
    case ClipId::StartRight:
        return "anims_apex/apex_walk_f.glb";
    case ClipId::StopForward:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::StopBackward:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::StopLeft:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::StopRight:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::PivotLeft:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::PivotRight:
        return "anims_apex/apex_idle_stand.glb";
    case ClipId::_Count:
        return "";
    }
    return "";
}

namespace
{
constexpr size_t k_clipCount = static_cast<size_t>(ClipId::_Count);
} // namespace

struct AnimationLibrary::Impl
{
    std::array<ozz::unique_ptr<ozz::animation::Animation>, k_clipCount> clips;
};

AnimationLibrary::AnimationLibrary() : impl_(std::make_unique<Impl>()) {}
AnimationLibrary::~AnimationLibrary() = default;
AnimationLibrary::AnimationLibrary(AnimationLibrary&&) noexcept = default;
AnimationLibrary& AnimationLibrary::operator=(AnimationLibrary&&) noexcept = default;

bool AnimationLibrary::has(ClipId id) const
{
    const size_t idx = static_cast<size_t>(id);
    return idx < k_clipCount && impl_->clips[idx] != nullptr;
}

const ozz::animation::Animation* AnimationLibrary::get(ClipId id) const
{
    const size_t idx = static_cast<size_t>(id);
    return (idx < k_clipCount) ? impl_->clips[idx].get() : nullptr;
}

float AnimationLibrary::duration(ClipId id) const
{
    const ozz::animation::Animation* anim = get(id);
    return anim ? anim->duration() : 0.0f;
}

bool AnimationLibrary::loadClipFromFBX(const CharacterRig& rig, ClipId id, const std::string& path)
{
    if (!rig.isLoaded() || !rig.skeleton()) {
        SDL_Log("AnimationLibrary: cannot load '%s' — rig is not loaded", path.c_str());
        return false;
    }

    const size_t slot = static_cast<size_t>(id);
    if (slot >= k_clipCount) {
        SDL_Log("AnimationLibrary: invalid ClipId %d", static_cast<int>(id));
        return false;
    }

    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    const auto flags =
        static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode) {
        SDL_Log("AnimationLibrary: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }
    if (scene->mNumAnimations == 0) {
        SDL_Log("AnimationLibrary: '%s' contains no animations", path.c_str());
        return false;
    }

    const aiAnimation* anim = scene->mAnimations[0];
    const double ticksPerSec = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 60.0;
    const float durationSec = static_cast<float>(anim->mDuration / ticksPerSec);

    // Channel lookup: node name → aiNodeAnim*.
    std::unordered_map<std::string, const aiNodeAnim*> channels;
    for (unsigned i = 0; i < anim->mNumChannels; ++i)
        channels[anim->mChannels[i]->mNodeName.C_Str()] = anim->mChannels[i];

    const ozz::animation::Skeleton* skel = rig.skeleton();
    const int numJoints = skel->num_joints();
    const auto jointNames = skel->joint_names();
    const auto& restPoses = rig.restPoses();

    ozz::animation::offline::RawAnimation raw;
    raw.duration = durationSec;
    raw.name = (anim->mName.length > 0) ? anim->mName.C_Str() : clipName(id);
    raw.tracks.resize(static_cast<size_t>(numJoints));

    for (int j = 0; j < numJoints; ++j) {
        auto& track = raw.tracks[static_cast<size_t>(j)];
        const std::string jointName(jointNames[static_cast<size_t>(j)]);

        auto chIt = channels.find(jointName);
        if (chIt != channels.end()) {
            const aiNodeAnim* ch = chIt->second;

            track.translations.reserve(ch->mNumPositionKeys);
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                const auto& key = ch->mPositionKeys[k];
                const float t = static_cast<float>(key.mTime / ticksPerSec);
                track.translations.push_back({t, ozz::math::Float3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            track.rotations.reserve(ch->mNumRotationKeys);
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const auto& key = ch->mRotationKeys[k];
                const float t = static_cast<float>(key.mTime / ticksPerSec);
                track.rotations.push_back(
                    {t, ozz::math::Quaternion{key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
            }

            track.scales.reserve(ch->mNumScalingKeys);
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                const auto& key = ch->mScalingKeys[k];
                const float t = static_cast<float>(key.mTime / ticksPerSec);
                track.scales.push_back({t, ozz::math::Float3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
        } else {
            // No animation channel for this joint — hold at the rig's rest pose.
            auto rpIt = restPoses.find(jointName);
            if (rpIt != restPoses.end()) {
                const auto& rp = rpIt->second;
                track.translations.push_back({0.f, rp.translation});
                track.rotations.push_back({0.f, rp.rotation});
                track.scales.push_back({0.f, rp.scale});
            } else {
                track.translations.push_back({0.f, ozz::math::Float3{0, 0, 0}});
                track.rotations.push_back({0.f, ozz::math::Quaternion{0, 0, 0, 1}});
                track.scales.push_back({0.f, ozz::math::Float3{1, 1, 1}});
            }
        }
    }

    // Strip horizontal root motion — Mixamo clips downloaded WITHOUT the
    // "In Place" toggle bake forward translation into the hip joint, which
    // makes the character drift during the loop and snap back at loop end.
    // Game movement is driven by physics/networking, so we want the character
    // to stay put visually and let the legs cycle in place.
    //
    // We cannot use `parent == kNoParent` here: `CharacterRig::loadFromFBX`
    // calls `buildJoint(scene->mRootNode, ...)`, so the ozz skeleton's root
    // joint is actually the FBX scene root (an unnamed structural node with
    // no translation keys).  The bone that actually carries root motion sits
    // a few joints below — `mixamorig:Hips` in Mixamo rigs.
    //
    // Fix: match by name.  Any joint whose name contains "Hips" has its
    // X/Z translation frozen to the first-frame value; Y (vertical bob) is
    // preserved.  This is the programmatic equivalent of Mixamo's "In Place"
    // export option.  Belt-and-braces: also strip the topmost translation-
    // carrying joint, so non-Mixamo rigs (or renamed bones) still get the
    // correct behaviour.
    {
        bool strippedAny = false;

        auto stripTrack = [&](int j, const char* reason) {
            auto& track = raw.tracks[static_cast<size_t>(j)];
            if (track.translations.empty())
                return false;

            const float lockedX = track.translations.front().value.x;
            const float lockedZ = track.translations.front().value.z;
            for (auto& key : track.translations) {
                key.value.x = lockedX;
                key.value.z = lockedZ;
            }
            SDL_Log("AnimationLibrary: locked XZ on joint '%s' (%s) in '%s'",
                    std::string(jointNames[static_cast<size_t>(j)]).c_str(),
                    reason,
                    path.c_str());
            return true;
        };

        // Named mover bones. Mixamo bakes root motion onto "mixamorig:Hips";
        // the Apex legend skeleton (Wraith) uses "jx_c_delta" (the "delta"
        // mover bone), with "def_c_hip" as the pelvis. Freeze XZ on any of them.
        for (int j = 0; j < numJoints; ++j) {
            const std::string jointName(jointNames[static_cast<size_t>(j)]);
            if (jointName.find("Hips") != std::string::npos || jointName.find("hip") != std::string::npos
                || jointName.find("Pelvis") != std::string::npos || jointName.find("pelvis") != std::string::npos
                || jointName.find("delta") != std::string::npos || jointName.find("Delta") != std::string::npos)
                strippedAny |= stripTrack(j, "name match: hip/pelvis/delta");
        }

        // Fallback ONLY if no named mover matched (non-Apex/Mixamo rig): freeze
        // the joint that travels the most horizontally. Guarded by !strippedAny
        // because once the real root mover (jx_c_delta / Hips) is frozen, the
        // "most-moving" remaining joint is a LEGITIMATELY animating bone (a foot
        // mid-step, or the weapon-attach `ja_c_propGun` that tracks the grip) —
        // freezing those would break the animation / detach the held weapon.
        // Attachment bones are skipped outright for the same reason.
        if (!strippedAny) {
            int moverJoint = -1;
            float bestSpan = 0.0f;
            for (int j = 0; j < numJoints; ++j) {
                const std::string jn(jointNames[static_cast<size_t>(j)]);
                if (jn.find("propGun") != std::string::npos || jn.find("propHand") != std::string::npos
                    || jn.find("weapon") != std::string::npos)
                    continue;
                const auto& tr = raw.tracks[static_cast<size_t>(j)].translations;
                if (tr.size() < 2)
                    continue;
                float minX = tr.front().value.x, maxX = minX;
                float minZ = tr.front().value.z, maxZ = minZ;
                for (const auto& key : tr) {
                    minX = std::min(minX, key.value.x);
                    maxX = std::max(maxX, key.value.x);
                    minZ = std::min(minZ, key.value.z);
                    maxZ = std::max(maxZ, key.value.z);
                }
                const float span = (maxX - minX) + (maxZ - minZ);
                if (span > bestSpan) {
                    bestSpan = span;
                    moverJoint = j;
                }
            }
            if (moverJoint >= 0 && bestSpan > 0.5f)
                stripTrack(moverJoint, "largest-horizontal-travel mover");
        }
    }

    if (!raw.Validate()) {
        SDL_Log("AnimationLibrary: raw animation validation failed for '%s'", path.c_str());
        return false;
    }

    ozz::animation::offline::AnimationBuilder builder;
    ozz::unique_ptr<ozz::animation::Animation> compiled = builder(raw);
    if (!compiled) {
        SDL_Log("AnimationLibrary: animation build failed for '%s'", path.c_str());
        return false;
    }

    SDL_Log("AnimationLibrary: loaded clip '%s' (%s) — duration=%.2fs, %u channels",
            clipName(id),
            path.c_str(),
            static_cast<double>(durationSec),
            anim->mNumChannels);

    impl_->clips[slot] = std::move(compiled);
    return true;
}
