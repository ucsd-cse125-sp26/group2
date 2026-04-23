/// @file SkinningBackend.cpp
/// @brief CPU linear-blend-skinning implementation.

#include "SkinningBackend.hpp"

#include <cstddef>

void CpuLbsSkinningBackend::skin(const std::vector<glm::mat4>& skinMats,
                                 const std::vector<ModelVertex>& baseVerts,
                                 const std::vector<SkinWeight>& weights,
                                 std::vector<ModelVertex>& outVerts) const
{
    const size_t numVerts = baseVerts.size();
    outVerts.resize(numVerts);

    for (size_t v = 0; v < numVerts; ++v) {
        const auto& base = baseVerts[v];
        const auto& sw = weights[v];
        auto& out = outVerts[v];

        glm::vec3 pos(0.0f);
        glm::vec3 norm(0.0f);
        glm::vec3 tang(0.0f);

        for (int i = 0; i < 4; ++i) {
            const float w = sw.weights[i];
            if (w <= 0.0f)
                continue;

            const size_t boneIdx = static_cast<size_t>(sw.boneIndices[i]);
            if (boneIdx >= skinMats.size())
                continue;

            const glm::mat4& mat = skinMats[boneIdx];
            const glm::mat3 nMat(mat); // rotation portion; correct for uniform scale.

            pos += w * glm::vec3(mat * glm::vec4(base.position, 1.0f));
            norm += w * (nMat * base.normal);
            tang += w * (nMat * glm::vec3(base.tangent));
        }

        out.position = pos;
        // Guard against zero-length normalize (degenerate weights).
        const float nLen2 = glm::dot(norm, norm);
        out.normal = (nLen2 > 1e-8f) ? norm * (1.0f / std::sqrt(nLen2)) : base.normal;
        const float tLen2 = glm::dot(tang, tang);
        const glm::vec3 tNorm = (tLen2 > 1e-8f) ? tang * (1.0f / std::sqrt(tLen2)) : glm::vec3(base.tangent);
        out.tangent = glm::vec4(tNorm, base.tangent.w);
        out.texCoord = base.texCoord; // UVs are invariant under skinning.
    }
}
