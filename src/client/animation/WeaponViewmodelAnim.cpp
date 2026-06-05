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
#include <ozz/animation/offline/additive_animation_builder.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/transform.h>
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
    /// Per-clip additive (delta) animations, taken relative to each clip's first
    /// frame. Layered on the base clip via BlendingJob for Apex-style state offsets
    /// (crouch lower, sprint sway, ...).
    std::unordered_map<std::string, ozz::unique_ptr<ozz::animation::Animation>> additiveClips;

    ozz::animation::SamplingJob::Context context;
    ozz::animation::SamplingJob::Context additiveContext;
    std::vector<ozz::math::SoaTransform> locals;     // blended result, num_soa_joints
    std::vector<ozz::math::SoaTransform> baseLocals;  // base clip sample
    std::vector<ozz::math::SoaTransform> addLocals;   // additive layer sample
    std::vector<ozz::math::Float4x4> models;      // num_joints
    std::vector<glm::mat4> skinMats;              // num_joints

    std::string additiveName;   ///< Active additive layer clip ("" = none).
    float additiveWeight = 0.0f; ///< 0..1 blend weight of the additive offset.
    float additiveRatio = 1.0f;  ///< 0..1 sample point in the additive clip (1 = end pose).

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
        src.materialIndex = mesh.materialIndex;
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
// Build a RawAnimation from an aiAnimation over the rig's skeleton, holding joints
// without a channel at their rest pose.  No root-motion stripping (the viewmodel is
// posed exactly as authored).
ozz::animation::offline::RawAnimation
buildRaw(const CharacterRig& rig, const aiAnimation* anim)
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

    return raw;
}

// Compile a RawAnimation to a runtime Animation.
ozz::unique_ptr<ozz::animation::Animation> buildClip(const ozz::animation::offline::RawAnimation& raw)
{
    if (!raw.Validate())
        return nullptr;
    ozz::animation::offline::AnimationBuilder builder;
    return builder(raw);
}

// Build a runtime *additive* (delta) Animation from a RawAnimation, taken relative
// to the skeleton REST pose (passed in as `ref`, one Transform per joint).
//
// Apex additive clips are authored as a delta relative to the neutral/rest pose and
// are typically CONSTANT over the clip (a held offset — the game ramps the additive
// WEIGHT over time, not the pose).  So referencing the clip's own first frame (the
// default) yields a zero delta.  Referencing the rest pose recovers the real per-
// joint offset (the cast importer stores additive rotations raw == relative to rest,
// so `delta = clip · rest⁻¹` is exactly the authored additive value).
ozz::unique_ptr<ozz::animation::Animation>
buildAdditiveClip(const ozz::animation::offline::RawAnimation& raw, ozz::span<const ozz::math::Transform> ref)
{
    if (!raw.Validate())
        return nullptr;
    ozz::animation::offline::RawAnimation deltaRaw;
    ozz::animation::offline::AdditiveAnimationBuilder addBuilder;
    if (!addBuilder(raw, ref, &deltaRaw) || !deltaRaw.Validate())
        return nullptr;
    ozz::animation::offline::AnimationBuilder builder;
    return builder(deltaRaw);
}

// Unpack one joint (lane 0..3) of a SoaTransform into an AoS ozz::math::Transform.
ozz::math::Transform soaJointToTransform(const ozz::math::SoaTransform& st, int lane)
{
    float tx[4], ty[4], tz[4], rx[4], ry[4], rz[4], rw[4], sx[4], sy[4], sz[4];
    ozz::math::StorePtrU(st.translation.x, tx);
    ozz::math::StorePtrU(st.translation.y, ty);
    ozz::math::StorePtrU(st.translation.z, tz);
    ozz::math::StorePtrU(st.rotation.x, rx);
    ozz::math::StorePtrU(st.rotation.y, ry);
    ozz::math::StorePtrU(st.rotation.z, rz);
    ozz::math::StorePtrU(st.rotation.w, rw);
    ozz::math::StorePtrU(st.scale.x, sx);
    ozz::math::StorePtrU(st.scale.y, sy);
    ozz::math::StorePtrU(st.scale.z, sz);
    ozz::math::Transform t;
    t.translation = ozz::math::Float3(tx[lane], ty[lane], tz[lane]);
    t.rotation = ozz::math::Quaternion(rx[lane], ry[lane], rz[lane], rw[lane]);
    t.scale = ozz::math::Float3(sx[lane], sy[lane], sz[lane]);
    return t;
}
} // namespace

bool WeaponViewmodelAnim::load(const std::string& glbPath, bool flipUVs)
{
    if (!impl_->rig.loadFromFBX(glbPath, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, flipUVs)) {
        SDL_Log("WeaponViewmodelAnim: failed to load rig from '%s'", glbPath.c_str());
        return false;
    }

    const int numJoints = impl_->rig.numJoints();
    impl_->context.Resize(numJoints);
    impl_->additiveContext.Resize(numJoints);
    const auto numSoa = static_cast<size_t>(impl_->rig.skeleton()->num_soa_joints());
    impl_->locals.resize(numSoa);
    impl_->baseLocals.resize(numSoa);
    impl_->addLocals.resize(numSoa);
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

    // Rest pose (AoS, one Transform per joint) — the reference for building additive
    // (delta) clips. Apex additive offsets are authored relative to the rest pose.
    std::vector<ozz::math::Transform> restRef(static_cast<size_t>(numJoints));
    {
        const auto jn = impl_->rig.skeleton()->joint_names();
        const auto& rp = impl_->rig.restPoses();
        for (int j = 0; j < numJoints; ++j) {
            ozz::math::Transform t;
            t.translation = ozz::math::Float3(0.f, 0.f, 0.f);
            t.rotation = ozz::math::Quaternion::identity();
            t.scale = ozz::math::Float3(1.f, 1.f, 1.f);
            auto rit = rp.find(std::string(jn[static_cast<size_t>(j)]));
            if (rit != rp.end()) {
                t.translation = rit->second.translation;
                t.rotation = rit->second.rotation;
                t.scale = rit->second.scale;
            }
            restRef[static_cast<size_t>(j)] = t;
        }
    }

    Assimp::Importer importer;
    const auto flags =
        static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);
    const aiScene* scene = importer.ReadFile(glbPath, flags);
    // Keep each clip's RawAnimation so the additive (delta) versions can be built in a
    // second pass, AFTER we know the neutral standing pose to reference against.
    std::vector<std::pair<std::string, ozz::animation::offline::RawAnimation>> rawClips;
    if (scene) {
        for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
            const aiAnimation* anim = scene->mAnimations[i];
            std::string name = (anim->mName.length > 0) ? anim->mName.C_Str() : ("clip" + std::to_string(i));
            // glTF/Assimp sometimes prefixes names; keep the trailing token after '|' if present.
            auto bar = name.rfind('|');
            if (bar != std::string::npos)
                name = name.substr(bar + 1);
            ozz::animation::offline::RawAnimation raw = buildRaw(impl_->rig, anim);
            ozz::unique_ptr<ozz::animation::Animation> compiled = buildClip(raw);
            if (compiled) {
                SDL_Log("WeaponViewmodelAnim: clip '%s' dur=%.2fs", name.c_str(), static_cast<double>(compiled->duration()));
                impl_->clips[name] = std::move(compiled);
                rawClips.emplace_back(name, std::move(raw));
            }
        }
    }

    // Additive reference = the neutral STANDING viewmodel pose, taken from the end of
    // the absolute "draw" clip (the aim-ready pose all the additive offsets layer onto).
    // Referencing the bind/rest pose instead would bake the root's bind→aim rotation
    // into every additive delta and roll the whole rig about its root on crouch.
    // Fall back to rest if "draw" is absent.
    std::vector<ozz::math::Transform> additiveRef = restRef;
    if (impl_->clips.count("draw") > 0) {
        std::vector<ozz::math::SoaTransform> tmp(static_cast<size_t>(impl_->rig.skeleton()->num_soa_joints()));
        ozz::animation::SamplingJob sj;
        sj.animation = impl_->clips["draw"].get();
        sj.context = &impl_->additiveContext;
        sj.ratio = 1.0f; // draw end = standing ready
        sj.output = ozz::make_span(tmp);
        if (sj.Run()) {
            for (int j = 0; j < numJoints; ++j)
                additiveRef[static_cast<size_t>(j)] = soaJointToTransform(tmp[static_cast<size_t>(j) / 4], j % 4);
        }
    }

    // Second pass: build the additive (delta) clips relative to the standing reference,
    // so any clip can be layered as an Apex-style offset (crouch/sprint/...) on top of
    // the base pose without disturbing the root.
    for (auto& [name, raw] : rawClips) {
        if (ozz::unique_ptr<ozz::animation::Animation> add = buildAdditiveClip(raw, ozz::make_span(additiveRef)))
            impl_->additiveClips[name] = std::move(add);
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

void WeaponViewmodelAnim::setAdditiveLayer(const std::string& name, float weight, float sampleRatio)
{
    impl_->additiveName = (impl_->additiveClips.count(name) > 0) ? name : std::string();
    impl_->additiveWeight = std::clamp(weight, 0.0f, 1.0f);
    impl_->additiveRatio = std::clamp(sampleRatio, 0.0f, 1.0f);
}

void WeaponViewmodelAnim::clearAdditiveLayer()
{
    impl_->additiveName.clear();
    impl_->additiveWeight = 0.0f;
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

bool WeaponViewmodelAnim::boneModelMatrix(const std::string& name, glm::mat4& out) const
{
    auto it = impl_->rig.jointMap().find(name);
    if (it == impl_->rig.jointMap().end())
        return false;
    const int idx = it->second;
    if (idx < 0 || idx >= static_cast<int>(impl_->jointModelMats.size()))
        return false;
    out = impl_->jointModelMats[static_cast<size_t>(idx)];
    return true;
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

            // Base clip -> baseLocals.
            ozz::animation::SamplingJob job;
            job.animation = clip;
            job.context = &impl_->context;
            job.ratio = ratio;
            job.output = ozz::make_span(impl_->baseLocals);
            if (job.Run()) {
                // Optionally layer an additive (delta) clip on top — Apex-style state
                // offsets (crouch lower, sprint sway). The delta is applied per-joint
                // and scaled by weight, so weight 0 == pure base, weight 1 == full
                // offset; ramping weight gives a smooth transition.
                auto addIt = (!impl_->additiveName.empty() && impl_->additiveWeight > 1e-3f)
                                 ? impl_->additiveClips.find(impl_->additiveName)
                                 : impl_->additiveClips.end();
                if (addIt != impl_->additiveClips.end() && addIt->second) {
                    ozz::animation::SamplingJob ajob;
                    ajob.animation = addIt->second.get();
                    ajob.context = &impl_->additiveContext;
                    ajob.ratio = impl_->additiveRatio;
                    ajob.output = ozz::make_span(impl_->addLocals);
                    ozz::animation::BlendingJob::Layer baseLayer;
                    baseLayer.transform = ozz::make_span(impl_->baseLocals);
                    baseLayer.weight = 1.0f;
                    ozz::animation::BlendingJob::Layer addLayer;
                    addLayer.transform = ozz::make_span(impl_->addLocals);
                    addLayer.weight = impl_->additiveWeight;
                    ozz::animation::BlendingJob bj;
                    bj.threshold = 0.1f;
                    bj.rest_pose = impl_->rig.skeleton()->joint_rest_poses();
                    bj.layers = ozz::span<const ozz::animation::BlendingJob::Layer>(&baseLayer, 1);
                    bj.additive_layers = ozz::span<const ozz::animation::BlendingJob::Layer>(&addLayer, 1);
                    bj.output = ozz::make_span(impl_->locals);
                    if (!(ajob.Run() && bj.Run()))
                        impl_->locals = impl_->baseLocals; // fall back to base on failure
                } else {
                    impl_->locals = impl_->baseLocals;
                }
                impl_->composeFromLocals();
                posed = true;
            }
        }
    }
    if (!posed)
        impl_->setRestPose();

    impl_->applyFireKick(dtSec);
}
