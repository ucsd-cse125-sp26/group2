/// @file CookedMeshFormat.hpp
/// @brief Binary on-disk format for cooked `WorldTriMesh` collision data.
///
/// Eliminates load-time BVH build + welding + V-HACD (the cooker runs all of
/// those once at content-build time and writes the result here).  Runtime
/// load is a single contiguous read + sanity check.
///
/// **Format.** Little-endian, 4-byte-aligned.  Versioned via the header
/// `magic` (`'g2cm'`) and `version` int.  Older cookers / loaders must
/// reject mismatched versions.
///
/// Layout:
///   Header                          (32 B)
///   Vertices (N * vec3)             (N * 12 B)
///   Indices (M * uint32)            (M * 4 B)
///   FaceNormals (Tri * vec3)        (Tri * 12 B)  Tri = M/3
///   EdgeActive (Tri * uint8)        (Tri B)
///   VertActive (Tri * uint8)        (Tri B)
///   EdgeNeighbor (Tri * 3 * u32)    (Tri * 12 B)  v2 — Phase B adjacency
///   TriangleMaterials (Tri * u8)    (Tri B) — 0xFF sentinel = none, use default
///   (Optional alignment padding to 4 B)
///   BVHNodes (K * BVHNode)          (K * sizeof(BVHNode))
///   TriIndices (Tri * uint32)       (Tri * 4 B)

#pragma once

#include "ecs/physics/SweptCollision.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace physics::cook
{

inline constexpr uint32_t k_magic = 0x6D63'3267u; // 'g2cm'
inline constexpr uint32_t k_version = 2u; // v2 adds edgeNeighbor (Phase B)

/// @brief 32-byte fixed-size header that opens every cooked-mesh blob.
struct Header
{
    uint32_t magic = k_magic;
    uint32_t version = k_version;
    uint32_t triCount = 0;
    uint32_t vertCount = 0;
    uint32_t bvhNodeCount = 0;
    uint32_t defaultSurface = 0; ///< SurfaceType
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

/// @brief Serialize a fully-cooked `WorldTriMesh` to a contiguous byte
/// vector.  The mesh must have been through `buildTriMeshBVH` + `weldTriMesh`
/// before calling — the blob is not regenerated, just copied out.
[[nodiscard]] std::vector<uint8_t> serialize(const WorldTriMesh& mesh);

/// @brief Deserialize a cooked blob back into a `WorldTriMesh`.  Returns
/// `false` on malformed / version-mismatched input (no partial population).
[[nodiscard]] bool deserialize(std::span<const uint8_t> blob, WorldTriMesh& out);

/// @brief Convenience: write a serialized blob to a file.  Returns false on
/// write failure (file path invalid, disk full, etc.).
[[nodiscard]] bool writeToFile(std::string_view path, const WorldTriMesh& mesh);

/// @brief Convenience: read a file into memory and deserialize.  Returns
/// false on missing file or invalid blob.
[[nodiscard]] bool readFromFile(std::string_view path, WorldTriMesh& out);

} // namespace physics::cook
