/// @file FbxImportUtils.hpp
/// @brief Assimp ↔ ozz ↔ GLM conversion helpers shared by the rig and animation library loaders.

#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <assimp/matrix4x4.h>
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
#include <ozz/base/maths/quaternion.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/vec_float.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace anim_utils
{

/// @brief Rest-pose local transform cached during skeleton building, reused
/// as a fallback keyframe for joints without animation channels.
struct JointRestPose
{
    ozz::math::Float3 translation{0, 0, 0};
    ozz::math::Quaternion rotation{0, 0, 0, 1};
    ozz::math::Float3 scale{1, 1, 1};
};

/// @brief Convert an ozz SIMD 4x4 matrix to a GLM column-major mat4.
/// @param m  Source ozz matrix.
/// @return Equivalent GLM mat4 (column-major).
glm::mat4 ozzToGlm(const ozz::math::Float4x4& m);

/// @brief Convert an Assimp row-major 4x4 matrix to GLM column-major.
/// @param m  Source assimp matrix.
/// @return Equivalent GLM mat4.
glm::mat4 aiToGlm(const aiMatrix4x4& m);

/// @brief Recursively build a RawSkeleton joint tree from the Assimp node hierarchy.
///
/// Only includes nodes that are bones or ancestors of bones (prunes mesh nodes
/// and other non-skeletal branches).
///
/// @param node       Current Assimp node to process.
/// @param boneNames  Set of bone names referenced by meshes.
/// @param outJoint   Output joint to populate.
/// @param restPoses  Map to store rest-pose transforms keyed by joint name.
/// @return True if this node or any descendant is a bone.
bool buildJoint(const aiNode* node,
                const std::unordered_set<std::string>& boneNames,
                ozz::animation::offline::RawSkeleton::Joint& outJoint,
                std::unordered_map<std::string, JointRestPose>& restPoses);

} // namespace anim_utils
