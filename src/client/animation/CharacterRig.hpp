/// @file CharacterRig.hpp
/// @brief Shared skinned-character rig: skeleton, bind-pose mesh, skin weights.

#pragma once

#include "FbxImportUtils.hpp"
#include "SkinVertex.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ozz::animation
{
class Skeleton;
}

/// @brief Per-vertex skin data — up to 4 bone influences, with parallel weights.
struct SkinWeight
{
    int boneIndices[4] = {0, 0, 0, 0};
    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// @brief Per-mesh bind-pose + skin weight data.  One entry per skinned mesh in the rig FBX.
struct RigMeshData
{
    std::vector<ModelVertex> baseVertices; ///< Bind-pose vertices (never mutated).
    std::vector<SkinWeight> skinWeights;   ///< Parallel to baseVertices.
    std::vector<uint32_t> indices;         ///< Triangle indices.
};

/// @brief Shared skinned rig — skeleton + bind-pose meshes + joint map.
///
/// Loaded once from a single FBX (the one containing skin data, e.g.
/// standard_walk.fbx).  Animation clips come from AnimationLibrary on top of
/// this rig.  Per-entity state lives in CharacterAnimator.
class CharacterRig
{
public:
    CharacterRig();
    ~CharacterRig();
    CharacterRig(const CharacterRig&) = delete;
    CharacterRig& operator=(const CharacterRig&) = delete;
    CharacterRig(CharacterRig&&) noexcept;
    CharacterRig& operator=(CharacterRig&&) noexcept;

    /// @brief Load rig from an FBX file.
    /// @param path  Absolute path to an FBX with a skin-weighted mesh.
    /// @return True on success (skeleton built + at least one skinned mesh).
    bool loadFromFBX(const std::string& path);

    /// @brief True after a successful loadFromFBX().
    [[nodiscard]] bool isLoaded() const noexcept;

    /// @brief Number of skeleton joints.  0 if not loaded.
    [[nodiscard]] int numJoints() const noexcept;

    /// @brief ozz skeleton (owning).  Null if not loaded.
    [[nodiscard]] const ozz::animation::Skeleton* skeleton() const noexcept;

    /// @brief Inverse bind matrices, one per joint (identity for structural joints).
    [[nodiscard]] const std::vector<glm::mat4>& inverseBindMatrices() const noexcept;

    /// @brief Per-mesh bind-pose + weight data.
    [[nodiscard]] const std::vector<RigMeshData>& meshes() const noexcept;

    /// @brief Joint name → runtime index map.
    [[nodiscard]] const std::unordered_map<std::string, int>& jointMap() const noexcept;

    /// @brief Rest-pose local transforms keyed by joint name (used by the
    /// animation library to build single-key tracks for joints that lack
    /// an animation channel in a given clip).
    [[nodiscard]] const std::unordered_map<std::string, anim_utils::JointRestPose>& restPoses() const noexcept;

    /// @brief Compute the vertical (Y) extent of the bind-pose mesh.
    ///
    /// Scans all vertices across all meshes and returns the minimum and
    /// maximum Y coordinates.  Used to auto-calculate the rig scale so the
    /// animated model matches the player's collision AABB height.
    void verticalBounds(float& outMinY, float& outMaxY) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
