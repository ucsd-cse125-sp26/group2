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

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

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
    case ClipId::EmoteFlair:
        return "Emote: Flair";
    case ClipId::EmoteMaraschino:
        return "Emote: Maraschino";
    case ClipId::EmoteGangnam:
        return "Emote: Gangnam";
    case ClipId::EmoteHipHop:
        return "Emote: Hip Hop";
    case ClipId::EmoteNorthernSoul:
        return "Emote: Northern Soul";
    case ClipId::_Count:
        return "(none)";
    }
    return "(unknown)";
}

const char* clipFile(ClipId id)
{
    switch (id) {
    case ClipId::Idle:
        return "male_locomotion_pack/idle.fbx";
    case ClipId::Walk:
        return "male_locomotion_pack/walking.fbx";
    case ClipId::Run:
        return "male_locomotion_pack/standard run.fbx";
    case ClipId::RunBackward:
        return "running_backward.fbx";
    case ClipId::SlowRun:
        return "slow_run.fbx";
    case ClipId::Slide:
        return "running_slide.fbx";
    case ClipId::WallRun:
        return "wall_run.fbx";
    case ClipId::Jump:
        return "male_locomotion_pack/jump.fbx";
    case ClipId::StrafeLeft:
        return "male_locomotion_pack/left strafe.fbx";
    case ClipId::StrafeRight:
        return "male_locomotion_pack/right strafe.fbx";
    case ClipId::StrafeLeftWalk:
        return "male_locomotion_pack/left strafe walking.fbx";
    case ClipId::StrafeRightWalk:
        return "male_locomotion_pack/right strafe walking.fbx";
    case ClipId::TurnLeft90:
        return "male_locomotion_pack/left turn 90.fbx";
    case ClipId::TurnRight90:
        return "male_locomotion_pack/right turn 90.fbx";
    case ClipId::CrouchIdle:
        return "crouch/Idle Crouching.fbx";
    case ClipId::CrouchWalk:
        return "crouch/Walk Crouching Forward.fbx";
    case ClipId::CrouchWalkLeft:
        return "crouch/Walk Crouching Left.fbx";
    case ClipId::CrouchWalkRight:
        return "crouch/Walk Crouching Right.fbx";
    case ClipId::CrouchWalkBackward:
        return "crouch/Walk Crouching Backward.fbx";
    case ClipId::StartForward:
        return "loco/start_forward.fbx";
    case ClipId::StartBackward:
        return "loco/start_backward.fbx";
    case ClipId::StartLeft:
        return "loco/start_left.fbx";
    case ClipId::StartRight:
        return "loco/start_right.fbx";
    case ClipId::StopForward:
        return "loco/stop_forward.fbx";
    case ClipId::StopBackward:
        return "loco/stop_backward.fbx";
    case ClipId::StopLeft:
        return "loco/stop_left.fbx";
    case ClipId::StopRight:
        return "loco/stop_right.fbx";
    case ClipId::PivotLeft:
        return "loco/pivot_left.fbx";
    case ClipId::PivotRight:
        return "loco/pivot_right.fbx";
    // Emotes live under assets/emotes/; clip paths are relative to
    // assets/animations/, so step up one directory.
    case ClipId::EmoteFlair:
        return "../emotes/Flair.fbx";
    case ClipId::EmoteMaraschino:
        return "../emotes/Dancing Maraschino Step.fbx";
    case ClipId::EmoteGangnam:
        return "../emotes/Gangnam Style.fbx";
    case ClipId::EmoteHipHop:
        return "../emotes/Hip Hop Dancing.fbx";
    case ClipId::EmoteNorthernSoul:
        return "../emotes/Northern Soul Floor Combo.fbx";
    case ClipId::_Count:
        return "";
    }
    return "";
}

ClipId emoteClipForIndex(int index)
{
    switch (index) {
    case 0:
        return ClipId::EmoteFlair;
    case 1:
        return ClipId::EmoteMaraschino;
    case 2:
        return ClipId::EmoteGangnam;
    case 3:
        return ClipId::EmoteHipHop;
    case 4:
        return ClipId::EmoteNorthernSoul;
    default:
        return ClipId::_Count;
    }
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

bool AnimationLibrary::loadClipFromFBX(const CharacterRig& rig, ClipId id, const std::string& path,
                                       bool useRigRestTranslations)
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

    // Retarget the clip's bone translations onto this rig's unit scale.
    //
    // Mixamo clips and the rig they're applied to can be authored at
    // different unit scales — e.g. a clip exported as a centimetre FBX vs a
    // rig baked to metres on glTF export. A bone's local translation is its
    // length/offset from its parent, which differs between two copies of the
    // SAME skeleton only by the uniform skeleton scale. If we copied clip
    // translations verbatim onto a differently-scaled rig the limbs would
    // stretch by that ratio. So compute a single uniform factor = (rig rest
    // translation magnitude) / (clip translation magnitude), taken as the
    // median over non-root bones that carry a meaningful translation, and
    // scale every clip translation by it. For a same-scale rig the factor is
    // ~1 (no-op), so existing rigs are unaffected.
    float clipScale = 1.0f;
    {
        std::vector<float> ratios;
        ratios.reserve(static_cast<size_t>(numJoints));
        for (int j = 0; j < numJoints; ++j) {
            const std::string jointName(jointNames[static_cast<size_t>(j)]);
            // Skip the root/hips: it carries animated (not fixed-length)
            // translation, so its magnitude isn't a clean scale proxy.
            if (jointName.find("Hips") != std::string::npos)
                continue;
            const auto chIt = channels.find(jointName);
            const auto rpIt = restPoses.find(jointName);
            if (chIt == channels.end() || rpIt == restPoses.end() || chIt->second->mNumPositionKeys == 0)
                continue;
            const aiVector3D cv = chIt->second->mPositionKeys[0].mValue;
            const float clipMag = std::sqrt(cv.x * cv.x + cv.y * cv.y + cv.z * cv.z);
            const ozz::math::Float3 rv = rpIt->second.translation;
            const float restMag = std::sqrt(rv.x * rv.x + rv.y * rv.y + rv.z * rv.z);
            if (clipMag > 1e-4f && restMag > 1e-6f)
                ratios.push_back(restMag / clipMag);
        }
        if (!ratios.empty()) {
            std::sort(ratios.begin(), ratios.end());
            clipScale = ratios[ratios.size() / 2]; // median is robust to per-bone noise
        }
    }
    if (std::abs(clipScale - 1.0f) > 0.01f)
        SDL_Log("AnimationLibrary: retargeting '%s' clip translations by scale %.5f", path.c_str(),
                static_cast<double>(clipScale));

    ozz::animation::offline::RawAnimation raw;
    raw.duration = durationSec;
    raw.name = (anim->mName.length > 0) ? anim->mName.C_Str() : clipName(id);
    raw.tracks.resize(static_cast<size_t>(numJoints));

    for (int j = 0; j < numJoints; ++j) {
        auto& track = raw.tracks[static_cast<size_t>(j)];
        const std::string jointName(jointNames[static_cast<size_t>(j)]);
        const bool isHipJoint = jointName.find("Hips") != std::string::npos;

        auto pushRigRestTranslationAndScale = [&]() {
            auto rpIt = restPoses.find(jointName);
            if (rpIt != restPoses.end()) {
                track.translations.push_back({0.f, rpIt->second.translation});
                track.scales.push_back({0.f, rpIt->second.scale});
            } else {
                track.translations.push_back({0.f, ozz::math::Float3{0, 0, 0}});
                track.scales.push_back({0.f, ozz::math::Float3{1, 1, 1}});
            }
        };

        auto chIt = channels.find(jointName);
        if (chIt != channels.end()) {
            const aiNodeAnim* ch = chIt->second;

            // Rotation-only retargeting: take the bone's translation and scale
            // from the rig's own rest pose (its native proportions) and let the
            // clip drive rotation only. Keeps a different-proportioned rig
            // grounded instead of inheriting the clip's hip height/root motion.
            if (useRigRestTranslations) {
                pushRigRestTranslationAndScale();
            } else {
                // Clip-local translations on non-root bones are source-rig bone
                // lengths, not gameplay motion. Importing them lets a movement
                // FBX with a different neck/head offset stretch our character.
                // Preserve hip Y motion for bob/crouch/slide, but keep every
                // other bone on this rig's native proportions. Scale keys are
                // always ignored for the same reason.
                if (isHipJoint) {
                    track.translations.reserve(ch->mNumPositionKeys);
                    for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                        const auto& key = ch->mPositionKeys[k];
                        const float t = static_cast<float>(key.mTime / ticksPerSec);
                        track.translations.push_back(
                            {t,
                             ozz::math::Float3{
                                 key.mValue.x * clipScale, key.mValue.y * clipScale, key.mValue.z * clipScale}});
                    }

                    auto rpIt = restPoses.find(jointName);
                    if (rpIt != restPoses.end())
                        track.scales.push_back({0.f, rpIt->second.scale});
                    else
                        track.scales.push_back({0.f, ozz::math::Float3{1, 1, 1}});
                } else {
                    pushRigRestTranslationAndScale();
                }
            }

            track.rotations.reserve(ch->mNumRotationKeys);
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const auto& key = ch->mRotationKeys[k];
                const float t = static_cast<float>(key.mTime / ticksPerSec);
                track.rotations.push_back(
                    {t, ozz::math::Quaternion{key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
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

        // Primary: every joint whose name contains "Hips" (Mixamo: "mixamorig:Hips").
        for (int j = 0; j < numJoints; ++j) {
            const std::string jointName(jointNames[static_cast<size_t>(j)]);
            if (jointName.find("Hips") != std::string::npos)
                strippedAny |= stripTrack(j, "name match: Hips");
        }

        // Fallback: if nothing matched by name (non-Mixamo rig), strip the
        // topmost joint with translation keys.  Ozz orders joints depth-first,
        // so the first populated track is the uppermost bone carrying motion.
        if (!strippedAny) {
            for (int j = 0; j < numJoints; ++j) {
                if (!raw.tracks[static_cast<size_t>(j)].translations.empty()) {
                    stripTrack(j, "fallback: topmost translation track");
                    break;
                }
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
