/// @file SkinnedModel.cpp
/// @brief Implementation of FBX loading, ozz skeleton/animation building, and CPU skinning.

#include "SkinnedModel.hpp"

#include <SDL3/SDL_log.h>

// Assimp headers
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

// ozz-animation headers
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// File-local types used by both Impl and helper functions.

namespace
{

/// @brief Rest-pose local transform cached during skeleton building, then reused
/// as a constant keyframe for joints that have no animation channel.
struct JointRestPose
{
    ozz::math::Float3 translation{0, 0, 0};
    ozz::math::Quaternion rotation{0, 0, 0, 1};
    ozz::math::Float3 scale{1, 1, 1};
};

struct SkinWeight
{
    int boneIndices[4]{0, 0, 0, 0};
    float weights[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

struct MeshSkinData
{
    std::vector<ModelVertex> baseVertices;    ///< Rest-pose vertices (never changed).
    std::vector<SkinWeight> skinWeights;      ///< Parallel to baseVertices.
    std::vector<ModelVertex> skinnedVertices; ///< Output — rewritten each update().
};

} // namespace

// PIMPL -- all ozz types are hidden from the header.

struct SkinnedModel::Impl
{
    // ozz runtime data
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    ozz::unique_ptr<ozz::animation::Animation> animation;
    ozz::animation::SamplingJob::Context context;

    // Runtime buffers (sized once after skeleton is built).
    std::vector<ozz::math::SoaTransform> locals; ///< SoA local-space transforms.
    std::vector<ozz::math::Float4x4> models;     ///< Per-joint model-space matrices.

    float playbackTime = 0.0f;

    std::vector<MeshSkinData> meshSkins;

    // Skeleton metadata
    /// Inverse bind matrix per skeleton joint.  Joints that are structural
    /// (not directly referenced as bones) keep identity.
    std::vector<glm::mat4> inverseBindMatrices;

    /// Preallocated skin matrices (model × inverseBindMatrix), one per joint.
    std::vector<glm::mat4> skinMatrices;

    /// Joint name → runtime index in the ozz skeleton.
    std::unordered_map<std::string, int> jointNameToIndex;

    std::unordered_map<std::string, JointRestPose> restPoses;

    /// Output model data (meshes + textures) for initial GPU upload.
    LoadedModel loadedModel;
    bool loaded = false;
};

// Anonymous helpers

namespace
{

/// @brief Convert an ozz SIMD 4x4 matrix to a GLM column-major mat4.
/// @param m  The ozz Float4x4 matrix.
/// @return Equivalent GLM mat4.
glm::mat4 ozzToGlm(const ozz::math::Float4x4& m)
{
    glm::mat4 out;
    // ozz Float4x4 stores 4 SIMD columns, same column-major layout as GLM.
    ozz::math::StorePtrU(m.cols[0], glm::value_ptr(out));
    ozz::math::StorePtrU(m.cols[1], glm::value_ptr(out) + 4);
    ozz::math::StorePtrU(m.cols[2], glm::value_ptr(out) + 8);
    ozz::math::StorePtrU(m.cols[3], glm::value_ptr(out) + 12);
    return out;
}

/// @brief Convert an Assimp row-major 4x4 matrix to GLM column-major.
/// @param m  The Assimp matrix.
/// @return Equivalent GLM mat4.
glm::mat4 aiToGlm(const aiMatrix4x4& m)
{
    return glm::transpose(glm::make_mat4(&m.a1));
}

// Skeleton building

/// @brief Recursively build a RawSkeleton joint tree from the Assimp node hierarchy.
///
/// Only includes nodes that are bones or ancestors of bones (prunes mesh
/// nodes and other non-skeletal branches).
///
/// @param node       Current Assimp node to process.
/// @param boneNames  Set of bone names referenced by meshes.
/// @param outJoint   Output joint to populate.
/// @param restPoses  Map to store rest-pose transforms for each joint.
/// @return True if this node or any descendant is a bone.
bool buildJoint(const aiNode* node,
                const std::unordered_set<std::string>& boneNames,
                ozz::animation::offline::RawSkeleton::Joint& outJoint,
                std::unordered_map<std::string, JointRestPose>& restPoses)
{
    const std::string name = node->mName.C_Str();
    outJoint.name = name;

    // Decompose the node's local transform into translation, rotation, scale.
    aiVector3D s;
    aiVector3D t;
    aiQuaternion r;
    node->mTransformation.Decompose(s, r, t);

    outJoint.transform.translation = ozz::math::Float3{t.x, t.y, t.z};
    outJoint.transform.rotation = ozz::math::Quaternion{r.x, r.y, r.z, r.w};
    outJoint.transform.scale = ozz::math::Float3{s.x, s.y, s.z};

    // Cache rest pose for building animation tracks of non-animated joints.
    restPoses[name] = JointRestPose{
        .translation = ozz::math::Float3{t.x, t.y, t.z},
        .rotation = ozz::math::Quaternion{r.x, r.y, r.z, r.w},
        .scale = ozz::math::Float3{s.x, s.y, s.z},
    };

    const bool isBone = boneNames.count(name) > 0;
    bool hasBoneChild = false;

    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        ozz::animation::offline::RawSkeleton::Joint child;
        if (buildJoint(node->mChildren[i], boneNames, child, restPoses)) {
            outJoint.children.push_back(std::move(child));
            hasBoneChild = true;
        }
    }

    return isBone || hasBoneChild;
}

} // namespace

// Constructor / destructor / move

SkinnedModel::SkinnedModel() : impl_(std::make_unique<Impl>()) {}
SkinnedModel::~SkinnedModel() = default;
SkinnedModel::SkinnedModel(SkinnedModel&&) noexcept = default;
SkinnedModel& SkinnedModel::operator=(SkinnedModel&&) noexcept = default;

// Simple getters (delegate to PIMPL)

const LoadedModel& SkinnedModel::getLoadedModel() const
{
    return impl_->loadedModel;
}

const std::vector<ModelVertex>& SkinnedModel::getSkinnedVertices(size_t meshIndex) const
{
    return impl_->meshSkins[meshIndex].skinnedVertices;
}

size_t SkinnedModel::meshCount() const
{
    return impl_->meshSkins.size();
}

float SkinnedModel::duration() const
{
    return impl_->animation ? impl_->animation->duration() : 0.0f;
}

bool SkinnedModel::isLoaded() const
{
    return impl_->loaded;
}

// Load -- parse FBX, build ozz skeleton + animation, extract skin weights.

bool SkinnedModel::load(const std::string& path)
{
    Assimp::Importer importer;

    // Collapse FBX-specific pivot/pre-rotation nodes into the parent's matrix
    // so the node hierarchy cleanly matches the bone hierarchy.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    const auto flags =
        static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
                                  aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode) {
        SDL_Log("SkinnedModel: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    if (scene->mNumAnimations == 0) {
        SDL_Log("SkinnedModel: '%s' contains no animations", path.c_str());
        return false;
    }

    // Step 1: Collect all bone names referenced by any mesh
    std::unordered_set<std::string> boneNames;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (unsigned b = 0; b < mesh->mNumBones; ++b)
            boneNames.insert(mesh->mBones[b]->mName.C_Str());
    }

    if (boneNames.empty()) {
        SDL_Log("SkinnedModel: '%s' has no bones — cannot skin", path.c_str());
        return false;
    }

    SDL_Log("SkinnedModel: found %zu bones in '%s'", boneNames.size(), path.c_str());

    // Step 2: Build ozz skeleton from the Assimp node hierarchy
    // Start from the scene root so the root's transform (FBX axis/unit
    // correction) is incorporated into the model-space matrices.
    ozz::animation::offline::RawSkeleton rawSkeleton;
    {
        ozz::animation::offline::RawSkeleton::Joint rootJoint;
        if (buildJoint(scene->mRootNode, boneNames, rootJoint, impl_->restPoses)) {
            rawSkeleton.roots.push_back(std::move(rootJoint));
        }
    }

    if (!rawSkeleton.Validate()) {
        SDL_Log("SkinnedModel: raw skeleton validation failed");
        return false;
    }

    ozz::animation::offline::SkeletonBuilder skelBuilder;
    impl_->skeleton = skelBuilder(rawSkeleton);
    if (!impl_->skeleton) {
        SDL_Log("SkinnedModel: skeleton build failed");
        return false;
    }

    const int numJoints = impl_->skeleton->num_joints();
    SDL_Log("SkinnedModel: skeleton built — %d joints", numJoints);

    // Build name → runtime-index map.
    const auto jointNames = impl_->skeleton->joint_names();
    for (int i = 0; i < numJoints; ++i)
        impl_->jointNameToIndex[std::string(jointNames[static_cast<size_t>(i)])] = i;

    // Step 3: Build ozz animation from the first Assimp animation
    const aiAnimation* anim = scene->mAnimations[0];
    const double ticksPerSec = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 60.0;
    const float durationSec = static_cast<float>(anim->mDuration / ticksPerSec);

    // Channel lookup: node name → aiNodeAnim*.
    std::unordered_map<std::string, const aiNodeAnim*> channels;
    for (unsigned i = 0; i < anim->mNumChannels; ++i)
        channels[anim->mChannels[i]->mNodeName.C_Str()] = anim->mChannels[i];

    ozz::animation::offline::RawAnimation rawAnimation;
    rawAnimation.duration = durationSec;
    rawAnimation.name = anim->mName.C_Str();
    rawAnimation.tracks.resize(static_cast<size_t>(numJoints));

    for (int i = 0; i < numJoints; ++i) {
        auto& track = rawAnimation.tracks[static_cast<size_t>(i)];
        const std::string jointName(jointNames[static_cast<size_t>(i)]);

        auto chIt = channels.find(jointName);
        if (chIt != channels.end()) {
            const aiNodeAnim* ch = chIt->second;

            // Translation keys
            track.translations.reserve(ch->mNumPositionKeys);
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                const auto& key = ch->mPositionKeys[k];
                const float t = static_cast<float>(key.mTime / ticksPerSec);
                track.translations.push_back({t, ozz::math::Float3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            // Rotation keys
            track.rotations.reserve(ch->mNumRotationKeys);
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const auto& key = ch->mRotationKeys[k];
                const float t = static_cast<float>(key.mTime / ticksPerSec);
                track.rotations.push_back(
                    {t, ozz::math::Quaternion{key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
            }

            // Scaling keys
            track.scales.reserve(ch->mNumScalingKeys);
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                const auto& key = ch->mScalingKeys[k];
                const float t = static_cast<float>(key.mTime / ticksPerSec);
                track.scales.push_back({t, ozz::math::Float3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
        } else {
            // No animation channel for this joint — hold at rest pose.
            auto rpIt = impl_->restPoses.find(jointName);
            if (rpIt != impl_->restPoses.end()) {
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

    if (!rawAnimation.Validate()) {
        SDL_Log("SkinnedModel: raw animation validation failed");
        return false;
    }

    ozz::animation::offline::AnimationBuilder animBuilder;
    impl_->animation = animBuilder(rawAnimation);
    if (!impl_->animation) {
        SDL_Log("SkinnedModel: animation build failed");
        return false;
    }

    SDL_Log("SkinnedModel: animation built — duration=%.2fs, %u channels",
            static_cast<double>(durationSec),
            anim->mNumChannels);

    // Step 4: Allocate runtime buffers
    impl_->locals.resize(static_cast<size_t>(impl_->skeleton->num_soa_joints()));
    impl_->models.resize(static_cast<size_t>(numJoints));
    impl_->context.Resize(numJoints);

    // Step 5: Collect inverse bind matrices (one per joint)
    // Non-bone joints (structural ancestors) keep identity.
    impl_->inverseBindMatrices.resize(static_cast<size_t>(numJoints), glm::mat4(1.0f));

    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (unsigned b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            auto it = impl_->jointNameToIndex.find(bone->mName.C_Str());
            if (it != impl_->jointNameToIndex.end())
                impl_->inverseBindMatrices[static_cast<size_t>(it->second)] = aiToGlm(bone->mOffsetMatrix);
        }
    }

    impl_->skinMatrices.resize(static_cast<size_t>(numJoints));

    // Step 6: Extract mesh vertices + skin weights
    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        if (!mesh->HasPositions() || !mesh->HasBones())
            continue;

        MeshSkinData skinData;
        skinData.baseVertices.resize(mesh->mNumVertices);
        skinData.skinWeights.resize(mesh->mNumVertices);

        // Extract vertices in mesh-local space (NOT world-transformed).
        // Bone transforms handle the positioning — no baked scene-graph here.
        const bool hasTangents = mesh->HasTangentsAndBitangents();
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex& vert = skinData.baseVertices[v];
            vert.position = glm::vec3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);

            if (mesh->HasNormals())
                vert.normal = glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);

            if (mesh->mTextureCoords[0] != nullptr)
                vert.texCoord = glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);

            if (hasTangents) {
                const glm::vec3 T(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
                const glm::vec3 B(mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z);
                const float w = (glm::dot(glm::cross(vert.normal, T), B) < 0.0f) ? -1.0f : 1.0f;
                vert.tangent = glm::vec4(T, w);
            } else {
                vert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
        }

        // Distribute bone weights to vertices (up to 4 per vertex).
        for (unsigned b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            auto it = impl_->jointNameToIndex.find(bone->mName.C_Str());
            if (it == impl_->jointNameToIndex.end())
                continue;
            const int jointIdx = it->second;

            for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                const unsigned vertIdx = bone->mWeights[w].mVertexId;
                const float weight = bone->mWeights[w].mWeight;
                if (vertIdx >= mesh->mNumVertices)
                    continue;

                auto& sw = skinData.skinWeights[vertIdx];
                for (int s = 0; s < 4; ++s) {
                    if (sw.weights[s] == 0.0f) {
                        sw.boneIndices[s] = jointIdx;
                        sw.weights[s] = weight;
                        break;
                    }
                }
            }
        }

        // Normalize weights so each vertex sums to 1.0.
        for (auto& sw : skinData.skinWeights) {
            const float total = sw.weights[0] + sw.weights[1] + sw.weights[2] + sw.weights[3];
            if (total > 0.0f) {
                const float inv = 1.0f / total;
                for (float& w : sw.weights)
                    w *= inv;
            } else {
                sw.weights[0] = 1.0f; // fallback: bind to joint 0 (root)
            }
        }

        skinData.skinnedVertices = skinData.baseVertices; // initial copy

        // Build MeshData for the LoadedModel (used by Renderer::uploadModel).
        MeshData meshData;
        meshData.vertices = skinData.baseVertices;

        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3)
                continue;
            meshData.indices.push_back(face.mIndices[0]);
            meshData.indices.push_back(face.mIndices[1]);
            meshData.indices.push_back(face.mIndices[2]);
        }

        // Default PBR material — grey matte (Mixamo FBXes seldom embed textures).
        meshData.material.baseColorFactor = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
        meshData.material.metallicFactor = 0.0f;
        meshData.material.roughnessFactor = 0.8f;

        impl_->loadedModel.meshes.push_back(std::move(meshData));
        impl_->meshSkins.push_back(std::move(skinData));
    }

    if (impl_->meshSkins.empty()) {
        SDL_Log("SkinnedModel: no skinned meshes found in '%s'", path.c_str());
        return false;
    }

    impl_->loaded = true;

    // Produce the first frame so getLoadedModel() returns valid geometry.
    update(0.0f);
    for (size_t i = 0; i < impl_->meshSkins.size(); ++i)
        impl_->loadedModel.meshes[i].vertices = impl_->meshSkins[i].skinnedVertices;

    SDL_Log("SkinnedModel: loaded '%s' — %zu mesh(es), %d joints, duration=%.2fs",
            path.c_str(),
            impl_->meshSkins.size(),
            numJoints,
            static_cast<double>(durationSec));
    return true;
}

// Update -- sample animation, local-to-model, CPU skinning.

void SkinnedModel::update(float dt)
{
    if (!impl_->loaded)
        return;

    // 1. Advance playback clock (looping)
    const float dur = impl_->animation->duration();
    impl_->playbackTime += dt;
    if (dur > 0.0f)
        impl_->playbackTime = std::fmod(impl_->playbackTime, dur);

    const float ratio = (dur > 0.0f) ? (impl_->playbackTime / dur) : 0.0f;

    // 2. Sample the animation at the current normalised time
    ozz::animation::SamplingJob sampling;
    sampling.animation = impl_->animation.get();
    sampling.context = &impl_->context;
    sampling.ratio = ratio;
    sampling.output = ozz::make_span(impl_->locals);
    if (!sampling.Run()) {
        SDL_Log("SkinnedModel: sampling job failed at ratio=%.3f", static_cast<double>(ratio));
        return;
    }

    // 3. Convert per-joint local transforms to model-space matrices
    ozz::animation::LocalToModelJob l2m;
    l2m.skeleton = impl_->skeleton.get();
    l2m.input = ozz::make_span(impl_->locals);
    l2m.output = ozz::make_span(impl_->models);
    if (!l2m.Run()) {
        SDL_Log("SkinnedModel: local-to-model job failed");
        return;
    }

    // 4. Precompute skin matrices: modelMatrix[j] * inverseBindMatrix[j]
    const int numJoints = impl_->skeleton->num_joints();
    for (int j = 0; j < numJoints; ++j)
        impl_->skinMatrices[static_cast<size_t>(j)] =
            ozzToGlm(impl_->models[static_cast<size_t>(j)]) * impl_->inverseBindMatrices[static_cast<size_t>(j)];

    // 5. Linear blend skinning -- transform each vertex by its bone weights
    for (auto& ms : impl_->meshSkins) {
        const size_t numVerts = ms.baseVertices.size();
        for (size_t v = 0; v < numVerts; ++v) {
            const auto& base = ms.baseVertices[v];
            const auto& sw = ms.skinWeights[v];
            auto& out = ms.skinnedVertices[v];

            glm::vec3 pos(0.0f);
            glm::vec3 norm(0.0f);
            glm::vec3 tang(0.0f);

            for (int i = 0; i < 4; ++i) {
                const float w = sw.weights[i];
                if (w <= 0.0f)
                    continue;

                const glm::mat4& mat = impl_->skinMatrices[static_cast<size_t>(sw.boneIndices[i])];
                const glm::mat3 nMat(mat); // rotation portion — correct for uniform scale

                pos += w * glm::vec3(mat * glm::vec4(base.position, 1.0f));
                norm += w * (nMat * base.normal);
                tang += w * (nMat * glm::vec3(base.tangent));
            }

            out.position = pos;
            out.normal = glm::normalize(norm);
            out.tangent = glm::vec4(glm::normalize(tang), base.tangent.w);
            out.texCoord = base.texCoord; // UVs are invariant under skinning
        }
    }
}
