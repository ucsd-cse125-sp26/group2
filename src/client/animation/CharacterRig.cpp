/// @file CharacterRig.cpp
/// @brief FBX loader that builds the ozz skeleton + bind-pose mesh data.

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
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/skeleton.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <limits>
#include <unordered_set>

struct CharacterRig::Impl
{
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<glm::mat4> inverseBindMatrices;
    std::vector<RigMeshData> meshes;
    std::unordered_map<std::string, int> jointMap;
    std::unordered_map<std::string, anim_utils::JointRestPose> restPoses;
    glm::quat orientationFix{1.0f, 0.0f, 0.0f, 0.0f}; ///< Applied to the skeleton root; mirrored in verticalBounds.
    bool loaded = false;
};

CharacterRig::CharacterRig() : impl_(std::make_unique<Impl>()) {}
CharacterRig::~CharacterRig() = default;
CharacterRig::CharacterRig(CharacterRig&&) noexcept = default;
CharacterRig& CharacterRig::operator=(CharacterRig&&) noexcept = default;

bool CharacterRig::isLoaded() const noexcept
{
    return impl_->loaded;
}

int CharacterRig::numJoints() const noexcept
{
    return impl_->skeleton ? impl_->skeleton->num_joints() : 0;
}

const ozz::animation::Skeleton* CharacterRig::skeleton() const noexcept
{
    return impl_->skeleton.get();
}

const std::vector<glm::mat4>& CharacterRig::inverseBindMatrices() const noexcept
{
    return impl_->inverseBindMatrices;
}

const std::vector<RigMeshData>& CharacterRig::meshes() const noexcept
{
    return impl_->meshes;
}

const std::unordered_map<std::string, int>& CharacterRig::jointMap() const noexcept
{
    return impl_->jointMap;
}

const std::unordered_map<std::string, anim_utils::JointRestPose>& CharacterRig::restPoses() const noexcept
{
    return impl_->restPoses;
}

void CharacterRig::verticalBounds(float& outMinY, float& outMaxY) const
{
    outMinY = std::numeric_limits<float>::max();
    outMaxY = std::numeric_limits<float>::lowest();
    // Measure in the rendered orientation: the skeleton root carries
    // `orientationFix`, so the rest-pose vertex actually drawn is
    // `orientationFix * position`. Auto-scale must use that Y extent or it
    // would scale to the wrong (pre-rotation) axis.
    for (const auto& mesh : impl_->meshes) {
        for (const auto& vert : mesh.baseVertices) {
            const float y = (impl_->orientationFix * vert.position).y;
            if (y < outMinY)
                outMinY = y;
            if (y > outMaxY)
                outMaxY = y;
        }
    }
    if (outMinY > outMaxY) {
        // Fallback if no vertices exist.
        outMinY = 0.0f;
        outMaxY = 1.0f;
    }
}

bool CharacterRig::loadFromFBX(const std::string& path, const glm::quat& orientationFix, bool flipNormals)
{
    impl_->orientationFix = orientationFix;

    Assimp::Importer importer;

    // Collapse FBX-specific pivot / pre-rotation nodes into the parent's
    // transform so the node hierarchy cleanly matches the bone hierarchy.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    const auto flags =
        static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
                                  aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode) {
        SDL_Log("CharacterRig: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    // 1. Collect bone names referenced by any mesh.
    std::unordered_set<std::string> boneNames;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (unsigned b = 0; b < mesh->mNumBones; ++b)
            boneNames.insert(mesh->mBones[b]->mName.C_Str());
    }
    if (boneNames.empty()) {
        SDL_Log("CharacterRig: '%s' has no bones — cannot skin", path.c_str());
        return false;
    }

    // 2. Build ozz skeleton from the Assimp node hierarchy, starting from the
    //    scene root so the root's axis/unit correction is baked in.
    ozz::animation::offline::RawSkeleton rawSkeleton;
    {
        ozz::animation::offline::RawSkeleton::Joint rootJoint;
        if (anim_utils::buildJoint(scene->mRootNode, boneNames, rootJoint, impl_->restPoses)) {
            // Prepend the orientation fix to the root joint. ozz propagates it
            // to every descendant in model space, so the whole rig re-orients
            // consistently (render mesh, joint frames used for weapon IK, and
            // hitbox capsules all derive from these matrices). The structural
            // root has no animation channel, so clips never overwrite it.
            if (orientationFix != glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
                const auto& tr = rootJoint.transform;
                const glm::vec3 t = orientationFix * glm::vec3(tr.translation.x, tr.translation.y, tr.translation.z);
                const glm::quat r =
                    orientationFix * glm::quat(tr.rotation.w, tr.rotation.x, tr.rotation.y, tr.rotation.z);
                rootJoint.transform.translation = ozz::math::Float3{t.x, t.y, t.z};
                rootJoint.transform.rotation = ozz::math::Quaternion{r.x, r.y, r.z, r.w};
                // Mirror into the rest-pose map: the structural root is filled
                // from rest (no channel), so the animated pose must see the fix.
                if (auto rp = impl_->restPoses.find(std::string(rootJoint.name.c_str())); rp != impl_->restPoses.end()) {
                    rp->second.translation = ozz::math::Float3{t.x, t.y, t.z};
                    rp->second.rotation = ozz::math::Quaternion{r.x, r.y, r.z, r.w};
                }
            }
            rawSkeleton.roots.push_back(std::move(rootJoint));
        }
    }
    if (!rawSkeleton.Validate()) {
        SDL_Log("CharacterRig: raw skeleton validation failed");
        return false;
    }
    ozz::animation::offline::SkeletonBuilder skelBuilder;
    impl_->skeleton = skelBuilder(rawSkeleton);
    if (!impl_->skeleton) {
        SDL_Log("CharacterRig: skeleton build failed");
        return false;
    }

    const int numJoints = impl_->skeleton->num_joints();
    SDL_Log("CharacterRig: skeleton built from '%s' — %d joints, %zu bones", path.c_str(), numJoints, boneNames.size());

    // Name → runtime-index map.
    const auto jointNames = impl_->skeleton->joint_names();
    for (int i = 0; i < numJoints; ++i)
        impl_->jointMap[std::string(jointNames[static_cast<size_t>(i)])] = i;

    // 3. Inverse bind matrices (identity for structural-only joints).
    impl_->inverseBindMatrices.assign(static_cast<size_t>(numJoints), glm::mat4(1.0f));
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (unsigned b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            auto it = impl_->jointMap.find(bone->mName.C_Str());
            if (it != impl_->jointMap.end())
                impl_->inverseBindMatrices[static_cast<size_t>(it->second)] = anim_utils::aiToGlm(bone->mOffsetMatrix);
        }
    }

    // 4. Extract mesh vertices + skin weights + indices.
    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        if (!mesh->HasPositions() || !mesh->HasBones())
            continue;

        RigMeshData rigMesh;
        rigMesh.baseVertices.resize(mesh->mNumVertices);
        rigMesh.skinWeights.resize(mesh->mNumVertices);

        const bool hasTangents = mesh->HasTangentsAndBitangents();
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex& vert = rigMesh.baseVertices[v];
            vert.position = glm::vec3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            if (mesh->HasNormals()) {
                vert.normal = glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                if (flipNormals)
                    vert.normal = -vert.normal;
            }
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

        // Distribute bone weights (up to 4 per vertex).
        for (unsigned b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            auto it = impl_->jointMap.find(bone->mName.C_Str());
            if (it == impl_->jointMap.end())
                continue;
            const int jointIdx = it->second;

            for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                const unsigned vertIdx = bone->mWeights[w].mVertexId;
                const float weight = bone->mWeights[w].mWeight;
                if (vertIdx >= mesh->mNumVertices)
                    continue;

                auto& sw = rigMesh.skinWeights[vertIdx];
                for (int s = 0; s < 4; ++s) {
                    if (sw.weights[s] == 0.0f) {
                        sw.boneIndices[s] = jointIdx;
                        sw.weights[s] = weight;
                        break;
                    }
                }
            }
        }

        // Normalise weights so each vertex sums to 1.  Fallback: bind to root.
        for (auto& sw : rigMesh.skinWeights) {
            const float total = sw.weights[0] + sw.weights[1] + sw.weights[2] + sw.weights[3];
            if (total > 0.0f) {
                const float inv = 1.0f / total;
                for (float& w : sw.weights)
                    w *= inv;
            } else {
                sw.weights[0] = 1.0f;
            }
        }

        // Indices.
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3)
                continue;
            rigMesh.indices.push_back(face.mIndices[0]);
            rigMesh.indices.push_back(face.mIndices[1]);
            rigMesh.indices.push_back(face.mIndices[2]);
        }

        impl_->meshes.push_back(std::move(rigMesh));
    }

    if (impl_->meshes.empty()) {
        SDL_Log("CharacterRig: no skinned meshes found in '%s'", path.c_str());
        return false;
    }

    impl_->loaded = true;
    SDL_Log("CharacterRig: loaded '%s' — %zu mesh(es), %d joints", path.c_str(), impl_->meshes.size(), numJoints);
    return true;
}
