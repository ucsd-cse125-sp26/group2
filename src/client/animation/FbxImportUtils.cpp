/// @file FbxImportUtils.cpp
/// @brief Implementation of Assimp ↔ ozz ↔ GLM conversion helpers.

#include "FbxImportUtils.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace anim_utils
{

glm::mat4 ozzToGlm(const ozz::math::Float4x4& m)
{
    glm::mat4 out;
    // ozz Float4x4 stores 4 SIMD columns in the same column-major layout as GLM.
    ozz::math::StorePtrU(m.cols[0], glm::value_ptr(out));
    ozz::math::StorePtrU(m.cols[1], glm::value_ptr(out) + 4);
    ozz::math::StorePtrU(m.cols[2], glm::value_ptr(out) + 8);
    ozz::math::StorePtrU(m.cols[3], glm::value_ptr(out) + 12);
    return out;
}

glm::mat4 aiToGlm(const aiMatrix4x4& m)
{
    return glm::transpose(glm::make_mat4(&m.a1));
}

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

    // Cache the rest pose so the animation library can fill tracks for joints
    // that lack animation channels (hold at rest).
    restPoses[name] = JointRestPose{
        .translation = ozz::math::Float3{t.x, t.y, t.z},
        .rotation = ozz::math::Quaternion{r.x, r.y, r.z, r.w},
        .scale = ozz::math::Float3{s.x, s.y, s.z},
    };

    // Keep skin-deforming bones, plus Apex weapon-attachment bones (which carry
    // no skin weights and would otherwise be pruned) such as `ja_c_propGun`.
    // The first-person viewmodel reads these reference frames for mounting; the
    // skin bones it also queries (muzzle_flash, def_c_bolt) are kept via boneNames.
    const bool isBone = boneNames.count(name) > 0 || name.find("propGun") != std::string::npos
                        || name.find("propHand") != std::string::npos || name.find("weapon_bone") != std::string::npos;
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

} // namespace anim_utils
