/// @file SkinnedModel.hpp
/// @brief Skinned mesh with skeletal animation loaded from FBX via Assimp and ozz-animation.

#pragma once

#include "renderer/ModelLoader.hpp"

#include <memory>
#include <string>
#include <vector>

/// @brief Skinned mesh with skeletal animation, loaded from FBX (Mixamo).
///
/// Uses Assimp to extract skeleton, skin weights, and animation from FBX.
/// Converts to ozz-animation runtime format for efficient CPU-side playback.
/// Each update() call advances the animation clock, samples the skeleton
/// pose, and rewrites the vertex buffer with linear-blend skinning.
///
/// Workflow:
///   1. load()            → parse FBX, build ozz skeleton + animation
///   2. getLoadedModel()  → hand to Renderer for initial GPU upload
///   3. update(dt) each frame → recompute skinned vertices
///   4. getSkinnedVertices(meshIdx) → re-upload to GPU vertex buffer
class SkinnedModel
{
public:
    SkinnedModel();
    ~SkinnedModel();
    SkinnedModel(SkinnedModel&&) noexcept;
    SkinnedModel& operator=(SkinnedModel&&) noexcept;

    SkinnedModel(const SkinnedModel&) = delete;
    SkinnedModel& operator=(const SkinnedModel&) = delete;

    /// @brief Load an FBX file containing skeleton, mesh, and animation data.
    /// @param path  Absolute path to the .fbx file.
    /// @return True on success (skeleton + at least one skinned mesh + animation).
    bool load(const std::string& path);

    /// @brief Advance animation by @p dt seconds (loops automatically) and
    /// recompute all CPU-skinned vertices.
    /// @param dt  Time step in seconds.
    void update(float dt);

    /// @brief Model data for initial GPU upload (first-frame vertices + indices + materials).
    /// @return Reference to the loaded model data. Valid after load() returns true.
    [[nodiscard]] const LoadedModel& getLoadedModel() const;

    /// @brief Current frame's skinned vertices for mesh @p meshIndex.
    /// @param meshIndex  Index of the mesh to query (default 0).
    /// @return Reference to the skinned vertex buffer, updated by update().
    [[nodiscard]] const std::vector<ModelVertex>& getSkinnedVertices(size_t meshIndex = 0) const;

    /// @brief Number of skinned meshes in the model.
    /// @return Mesh count.
    [[nodiscard]] size_t meshCount() const;

    /// @brief Animation clip duration in seconds.
    /// @return Duration in seconds, or 0 if no animation is loaded.
    [[nodiscard]] float duration() const;

    /// @brief True after a successful load().
    /// @return Whether model data is available.
    [[nodiscard]] bool isLoaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
