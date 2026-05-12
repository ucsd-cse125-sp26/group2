/// @file SkinningBackend.hpp
/// @brief Pluggable skinning interface — phase-1 CPU LBS; GPU backend slots in later.

#pragma once

#include "CharacterRig.hpp"
#include "SkinVertex.hpp"

#include <glm/glm.hpp>
#include <vector>

/// @brief Abstract skinning backend.  Transforms bind-pose vertices into
/// animated vertices using per-joint skin matrices and per-vertex weights.
///
/// Phase-1: CpuLbsSkinningBackend performs the transform on the CPU and the
/// caller streams the result into a per-entity vertex buffer.
/// Phase-2: GpuSkinningBackend will upload skin matrices + let a vertex
/// shader do the transform, skipping the CPU pass entirely.
class ISkinningBackend
{
public:
    virtual ~ISkinningBackend() = default;

    /// @brief Skin one mesh in-place.
    /// @param skinMats    One matrix per skeleton joint (modelMat * invBind).
    /// @param baseVerts   Bind-pose vertices (read-only).
    /// @param weights     Per-vertex bone weights (parallel to @p baseVerts).
    /// @param outVerts    Output buffer.  Resized to baseVerts.size() inside.
    virtual void skin(const std::vector<glm::mat4>& skinMats,
                      const std::vector<ModelVertex>& baseVerts,
                      const std::vector<SkinWeight>& weights,
                      std::vector<ModelVertex>& outVerts) const = 0;
};

/// @brief CPU Linear Blend Skinning — pure function; no state between calls.
class CpuLbsSkinningBackend : public ISkinningBackend
{
public:
    void skin(const std::vector<glm::mat4>& skinMats,
              const std::vector<ModelVertex>& baseVerts,
              const std::vector<SkinWeight>& weights,
              std::vector<ModelVertex>& outVerts) const override;
};
