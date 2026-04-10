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

    /// Load an FBX file containing skeleton, mesh, and animation data.
    /// @param path  Absolute path to the .fbx file.
    /// @return True on success (skeleton + at least one skinned mesh + animation).
    bool load(const std::string& path);

    /// Advance animation by @p dt seconds (loops automatically) and
    /// recompute all CPU-skinned vertices.
    void update(float dt);

    /// Model data for initial GPU upload (first-frame vertices + indices + materials).
    /// Valid after load() returns true.
    [[nodiscard]] const LoadedModel& getLoadedModel() const;

    /// Current frame's skinned vertices for mesh @p meshIndex.
    /// Updated by update().
    [[nodiscard]] const std::vector<ModelVertex>& getSkinnedVertices(size_t meshIndex = 0) const;

    /// Number of skinned meshes in the model.
    [[nodiscard]] size_t meshCount() const;

    /// Animation clip duration in seconds.
    [[nodiscard]] float duration() const;

    /// True after a successful load().
    [[nodiscard]] bool isLoaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
