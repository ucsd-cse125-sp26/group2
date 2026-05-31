/// @file WeaponViewmodelAnim.cpp
#include "WeaponViewmodelAnim.hpp"

#include "FbxImportUtils.hpp" // anim_utils::ozzToGlm

#include <SDL3/SDL_log.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <assimp/Importer.hpp>
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
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <unordered_map>

struct WeaponViewmodelAnim::Impl
{
    CharacterRig rig;
    std::unordered_map<std::string, ozz::unique_ptr<ozz::animation::Animation>> clips;

    ozz::animation::SamplingJob::Context context;
    std::vector<ozz::math::SoaTransform> locals; // num_soa_joints
    std::vector<ozz::math::Float4x4> models;      // num_joints
    std::vector<glm::mat4> skinMats;              // num_joints

    std::string curClip;
    float time = 0.0f;
    float duration = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool finished = false;

    int boltJoint = -1;     ///< def_c_bolt index (gun charging handle/bolt), -1 if absent.
    float fireKick = 0.0f;  ///< 1 just after a shot, decays to 0 over one bolt cycle.
    glm::vec3 boltSlideAxis{0.0f, 0.0f, -1.0f}; ///< Model-space "backward" (muzzle->bolt), derived at load.
    std::vector<glm::mat4> jointModelMats;      ///< Model-space bone matrices this frame (for bone queries).

    void composeFromLocals()
    {
        ozz::animation::LocalToModelJob l2m;
        l2m.skeleton = rig.skeleton();
        l2m.input = ozz::make_span(locals);
        l2m.output = ozz::make_span(models);
        if (!l2m.Run())
            return;
        const auto& ibm = rig.inverseBindMatrices();
        const int n = rig.numJoints();
        for (int j = 0; j < n; ++j) {
            const glm::mat4 jm = anim_utils::ozzToGlm(models[static_cast<size_t>(j)]);
            jointModelMats[static_cast<size_t>(j)] = jm;
            skinMats[static_cast<size_t>(j)] = jm * ibm[static_cast<size_t>(j)];
        }
    }

    void setRestPose()
    {
        const auto rest = rig.skeleton()->joint_rest_poses();
        std::copy(rest.begin(), rest.end(), locals.begin());
        composeFromLocals();
    }

    // Overlay a quick bolt/charging-handle slide on top of the composed pose.
    void applyFireKick(float dtSec)
    {
        if (fireKick > 0.0f) {
            fireKick -= dtSec / 0.055f; // ~55ms cycle
            if (fireKick < 0.0f)
                fireKick = 0.0f;
        }
        if (boltJoint >= 0 && boltJoint < static_cast<int>(skinMats.size()) && fireKick > 0.0f) {
            // Slide backward along the model-derived barrel axis (muzzle->bolt).
            const float dist = 4.0f * fireKick;
            skinMats[static_cast<size_t>(boltJoint)] =
                glm::translate(glm::mat4(1.0f), boltSlideAxis * dist) * skinMats[static_cast<size_t>(boltJoint)];
        }
    }
};

WeaponViewmodelAnim::WeaponViewmodelAnim() : impl_(std::make_unique<Impl>()) {}
WeaponViewmodelAnim::~WeaponViewmodelAnim() = default;

bool WeaponViewmodelAnim::isLoaded() const { return impl_->rig.isLoaded(); }
int WeaponViewmodelAnim::numJoints() const { return impl_->rig.numJoints(); }
const std::string& WeaponViewmodelAnim::currentClip() const { return impl_->curClip; }
bool WeaponViewmodelAnim::clipFinished() const { return impl_->finished; }
const std::vector<glm::mat4>& WeaponViewmodelAnim::skinMatrices() const { return impl_->skinMats; }
bool WeaponViewmodelAnim::hasClip(const std::string& name) const { return impl_->clips.count(name) > 0; }

float WeaponViewmodelAnim::clipDuration(const std::string& name) const
{
    auto it = impl_->clips.find(name);
    return (it != impl_->clips.end() && it->second) ? it->second->duration() : 0.0f;
}

std::vector<RigMeshSource> WeaponViewmodelAnim::buildRigSources() const
{
    std::vector<RigMeshSource> sources;
    sources.reserve(impl_->rig.meshes().size());
    for (const RigMeshData& mesh : impl_->rig.meshes()) {
        RigMeshSource src;
        src.bindPoseVertices = mesh.baseVertices;
        src.indices = mesh.indices;
        src.boneInfluences.reserve(mesh.skinWeights.size());
        for (const SkinWeight& w : mesh.skinWeights) {
            BoneInfluence bi;
            for (int i = 0; i < 4; ++i) {
                bi.boneIndices[i] = w.boneIndices[i];
                bi.boneWeights[i] = w.weights[i];
            }
            src.boneInfluences.push_back(bi);
        }
        sources.push_back(std::move(src));
    }
    return sources;
}

namespace
{
// Build one ozz Animation from an aiAnimation over the rig's skeleton, holding
// joints without a channel at their rest pose.  No root-motion stripping (the
// viewmodel is posed exactly as authored).
ozz::unique_ptr<ozz::animation::Animation>
buildClip(const CharacterRig& rig, const aiAnimation* anim)
{
    const double ticksPerSec = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 60.0;
    const float durationSec = static_cast<float>(anim->mDuration / ticksPerSec);

    std::unordered_map<std::string, const aiNodeAnim*> channels;
    for (unsigned i = 0; i < anim->mNumChannels; ++i)
        channels[anim->mChannels[i]->mNodeName.C_Str()] = anim->mChannels[i];

    const ozz::animation::Skeleton* skel = rig.skeleton();
    const int numJoints = skel->num_joints();
    const auto jointNames = skel->joint_names();
    const auto& restPoses = rig.restPoses();

    ozz::animation::offline::RawAnimation raw;
    raw.duration = (durationSec > 0.0f) ? durationSec : (1.0f / 30.0f);
    raw.tracks.resize(static_cast<size_t>(numJoints));

    for (int j = 0; j < numJoints; ++j) {
        auto& track = raw.tracks[static_cast<size_t>(j)];
        const std::string jointName(jointNames[static_cast<size_t>(j)]);
        auto chIt = channels.find(jointName);
        if (chIt != channels.end()) {
            const aiNodeAnim* ch = chIt->second;
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                const auto& key = ch->mPositionKeys[k];
                track.translations.push_back(
                    {static_cast<float>(key.mTime / ticksPerSec), ozz::math::Float3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const auto& key = ch->mRotationKeys[k];
                track.rotations.push_back(
                    {static_cast<float>(key.mTime / ticksPerSec),
                     ozz::math::Quaternion{key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                const auto& key = ch->mScalingKeys[k];
                track.scales.push_back(
                    {static_cast<float>(key.mTime / ticksPerSec), ozz::math::Float3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
        }
        if (track.translations.empty() || track.rotations.empty() || track.scales.empty()) {
            auto rpIt = restPoses.find(jointName);
            ozz::math::Float3 t{0, 0, 0}, s{1, 1, 1};
            ozz::math::Quaternion r{0, 0, 0, 1};
            if (rpIt != restPoses.end()) {
                t = rpIt->second.translation;
                r = rpIt->second.rotation;
                s = rpIt->second.scale;
            }
            if (track.translations.empty())
                track.translations.push_back({0.f, t});
            if (track.rotations.empty())
                track.rotations.push_back({0.f, r});
            if (track.scales.empty())
                track.scales.push_back({0.f, s});
        }
    }

    if (!raw.Validate())
        return nullptr;
    ozz::animation::offline::AnimationBuilder builder;
    return builder(raw);
}
} // namespace

bool WeaponViewmodelAnim::load(const std::string& glbPath, bool flipUVs)
{
    if (!impl_->rig.loadFromFBX(glbPath, flipUVs)) {
        SDL_Log("WeaponViewmodelAnim: failed to load rig from '%s'", glbPath.c_str());
        return false;
    }

    const int numJoints = impl_->rig.numJoints();
    impl_->context.Resize(numJoints);
    impl_->locals.resize(static_cast<size_t>(impl_->rig.skeleton()->num_soa_joints()));
    impl_->models.resize(static_cast<size_t>(numJoints));
    impl_->skinMats.assign(static_cast<size_t>(numJoints), glm::mat4(1.0f));

    impl_->jointModelMats.assign(static_cast<size_t>(numJoints), glm::mat4(1.0f));

    auto boltIt = impl_->rig.jointMap().find("def_c_bolt");
    impl_->boltJoint = (boltIt != impl_->rig.jointMap().end()) ? boltIt->second : -1;

    // Derive the bolt's slide axis from the rig itself: the bind-pose direction
    // muzzle_flash -> def_c_bolt is "backward" along the barrel.
    auto muzIt = impl_->rig.jointMap().find("muzzle_flash");
    const int muzJoint = (muzIt != impl_->rig.jointMap().end()) ? muzIt->second : -1;
    if (impl_->boltJoint >= 0 && muzJoint >= 0) {
        const auto& ibm = impl_->rig.inverseBindMatrices();
        const glm::vec3 boltPos = glm::vec3(glm::inverse(ibm[static_cast<size_t>(impl_->boltJoint)])[3]);
        const glm::vec3 muzPos = glm::vec3(glm::inverse(ibm[static_cast<size_t>(muzJoint)])[3]);
        const glm::vec3 d = boltPos - muzPos;
        if (glm::length(d) > 1e-4f)
            impl_->boltSlideAxis = glm::normalize(d);
    }

    Assimp::Importer importer;
    const auto flags =
        static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);
    const aiScene* scene = importer.ReadFile(glbPath, flags);
    if (scene) {
        for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
            const aiAnimation* anim = scene->mAnimations[i];
            std::string name = (anim->mName.length > 0) ? anim->mName.C_Str() : ("clip" + std::to_string(i));
            // glTF/Assimp sometimes prefixes names; keep the trailing token after '|' if present.
            auto bar = name.rfind('|');
            if (bar != std::string::npos)
                name = name.substr(bar + 1);
            ozz::unique_ptr<ozz::animation::Animation> compiled = buildClip(impl_->rig, anim);
            if (compiled) {
                SDL_Log("WeaponViewmodelAnim: clip '%s' dur=%.2fs", name.c_str(), static_cast<double>(compiled->duration()));
                impl_->clips[name] = std::move(compiled);
            }
        }
    }
    SDL_Log("WeaponViewmodelAnim: loaded '%s' — %d joints, %zu clips",
            glbPath.c_str(),
            numJoints,
            impl_->clips.size());

    impl_->setRestPose();
    return true;
}

void WeaponViewmodelAnim::playClip(const std::string& name, bool loop, float speed)
{
    if (!hasClip(name)) {
        playRestPose();
        return;
    }
    impl_->curClip = name;
    impl_->loop = loop;
    impl_->speed = (speed > 0.0001f) ? speed : 1.0f;
    impl_->time = 0.0f;
    impl_->duration = impl_->clips[name]->duration();
    impl_->finished = false;
}

void WeaponViewmodelAnim::playRestPose()
{
    impl_->curClip.clear();
    impl_->finished = false;
    if (impl_->rig.isLoaded())
        impl_->setRestPose();
}

void WeaponViewmodelAnim::triggerFire()
{
    impl_->fireKick = 1.0f;
}

glm::vec3 WeaponViewmodelAnim::boneModelPos(const std::string& name) const
{
    auto it = impl_->rig.jointMap().find(name);
    if (it == impl_->rig.jointMap().end())
        return glm::vec3(0.0f);
    const int idx = it->second;
    if (idx < 0 || idx >= static_cast<int>(impl_->jointModelMats.size()))
        return glm::vec3(0.0f);
    return glm::vec3(impl_->jointModelMats[static_cast<size_t>(idx)][3]);
}

void WeaponViewmodelAnim::update(float dtSec)
{
    if (!impl_->rig.isLoaded())
        return;

    bool posed = false;
    if (!impl_->curClip.empty()) {
        auto it = impl_->clips.find(impl_->curClip);
        if (it != impl_->clips.end() && it->second) {
            const ozz::animation::Animation* clip = it->second.get();
            const float dur = (impl_->duration > 0.0001f) ? impl_->duration : clip->duration();

            impl_->time += dtSec * impl_->speed;
            float ratio = (dur > 0.0001f) ? (impl_->time / dur) : 1.0f;
            if (impl_->loop) {
                ratio = ratio - std::floor(ratio);
            } else if (ratio >= 1.0f) {
                ratio = 1.0f;
                impl_->finished = true;
            }

            ozz::animation::SamplingJob job;
            job.animation = clip;
            job.context = &impl_->context;
            job.ratio = ratio;
            job.output = ozz::make_span(impl_->locals);
            if (job.Run()) {
                impl_->composeFromLocals();
                posed = true;
            }
        }
    }
    if (!posed)
        impl_->setRestPose();

    impl_->applyFireKick(dtSec);
}
