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
    case ClipId::_Count:
        return "(none)";
    }
    return "(unknown)";
}

const char* clipFile(ClipId id)
{
    switch (id) {
    case ClipId::Idle:
        return "idle.fbx";
    case ClipId::Walk:
        return "standard_walk.fbx";
    case ClipId::Run:
        return "running.fbx";
    case ClipId::RunBackward:
        return "running_backward.fbx";
    case ClipId::SlowRun:
        return "slow_run.fbx";
    case ClipId::Slide:
        return "running_slide.fbx";
    case ClipId::WallRun:
        return "wall_run.fbx";
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
    // Fix: for every root joint (parent == kNoParent — in a Mixamo rig this
    // is `mixamorig:Hips`), freeze translation X/Z to the first-frame value
    // while leaving Y (vertical bob) intact.  This is the programmatic
    // equivalent of Mixamo's "In Place" export option.
    {
        const auto jointParents = skel->joint_parents();
        for (int j = 0; j < numJoints; ++j) {
            if (jointParents[static_cast<size_t>(j)] != ozz::animation::Skeleton::kNoParent)
                continue;

            auto& track = raw.tracks[static_cast<size_t>(j)];
            if (track.translations.empty())
                continue;

            const float lockedX = track.translations.front().value.x;
            const float lockedZ = track.translations.front().value.z;
            for (auto& key : track.translations) {
                key.value.x = lockedX;
                key.value.z = lockedZ;
            }
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
