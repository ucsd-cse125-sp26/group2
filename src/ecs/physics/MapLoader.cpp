/// @file MapLoader.cpp
/// @brief Assimp-based collision extraction from map GLB files.
///
/// Auto-detects the best collision primitive for each mesh:
///   sphere → cylinder → axis-aligned box → convex brush (fallback).

#include "MapLoader.hpp"

#include "TriMeshCollision.hpp"

#include <SDL3/SDL_log.h>

// V-HACD (header-only).  Implementation is compiled in VHACDImpl.cpp via
// `#define ENABLE_VHACD_IMPLEMENTATION` — this site only needs the API.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <VHACD.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

namespace physics
{

namespace
{

/// @brief Convert an Assimp 4×4 matrix to a glm column-major matrix.
glm::mat4 aiToGlm(const aiMatrix4x4& m)
{
    return glm::transpose(glm::make_mat4(&m.a1));
}

/// @brief Case-insensitive substring search.
bool containsCI(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    auto toLower = [](const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    };
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

/// @brief Check whether any ancestor node name contains the given collection name.
bool isUnderCollectionNode(const aiNode* node, const std::string& collectionName)
{
    const aiNode* cur = node->mParent;
    while (cur != nullptr) {
        if (containsCI(std::string(cur->mName.C_Str()), collectionName))
            return true;
        cur = cur->mParent;
    }
    return false;
}

/// @brief Determine the collision sub-collection type from ancestor node names.
/// Returns: "boxes", "brushes", "cylinders", "spheres", "meshes", or "" (auto).
///
/// Two Blender authoring conventions are supported:
///   1. **Sub-collection (plural):** wrap collision objects in a child collection
///      named "Boxes", "Cylinders", "Spheres", "Brushes", or "Meshes".  The
///      collection becomes a parent node in the GLB; we walk ancestors here.
///   2. **Object name (singular):** name the collision object after its shape
///      (the Blender default for primitive adds — "Cylinder", "Sphere", etc.).
///      Useful when collision is tagged by name prefix (e.g. "COL_Cylinder")
///      instead of nested sub-collections.
///
/// The node's own name is checked first because it is the more specific signal:
/// users who deliberately group meshes under a sub-collection are typically
/// fine with auto-detection for the individual meshes, but a mesh actually
/// named "Cylinder" almost always *is* the Blender cylinder primitive (which
/// auto-detection mis-fits as a sphere when the cylinder is rotated off-Y).
std::string getCollectionType(const aiNode* node)
{
    // 1) Check the node's own name for Blender-style shape keywords.  Only
    //    "cylinder" needs help today: the auto AABB / fitSphere paths already
    //    handle "Cube"/"Plane"/"Sphere"/"Icosphere" correctly, but a rotated
    //    cylinder has equidistant rim vertices and slips into fitSphere.
    {
        const std::string ownName(node->mName.C_Str());
        if (containsCI(ownName, "cylinder"))
            return "cylinders";
    }

    // 2) Walk ancestors looking for sub-collection names (plural).
    const aiNode* cur = node->mParent;
    while (cur != nullptr) {
        std::string name(cur->mName.C_Str());
        if (containsCI(name, "boxes"))
            return "boxes";
        if (containsCI(name, "brushes"))
            return "brushes";
        if (containsCI(name, "cylinders"))
            return "cylinders";
        if (containsCI(name, "spheres"))
            return "spheres";
        if (containsCI(name, "meshes"))
            return "meshes";
        cur = cur->mParent;
    }
    return ""; // auto-detect
}

/// @brief Walk the scene-graph ancestry and accumulate the world transform.
glm::mat4 accumulatedTransform(const aiNode* node)
{
    glm::mat4 t(1.0f);
    std::vector<const aiNode*> chain;
    const aiNode* cur = node;
    while (cur != nullptr) {
        chain.push_back(cur);
        cur = cur->mParent;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        t = t * aiToGlm((*it)->mTransformation);
    return t;
}

/// @brief Transform all mesh vertices to world space with uniform scale.
std::vector<glm::vec3> getWorldVertices(const aiMesh* mesh, const glm::mat4& world, float scale)
{
    std::vector<glm::vec3> verts;
    if (!mesh->HasPositions())
        return verts;
    verts.reserve(mesh->mNumVertices);
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
        const glm::vec4 local(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f);
        verts.push_back(glm::vec3(world * local) * scale);
    }
    return verts;
}

// Shape fitting

/// @brief Compute AABB from vertices.
void computeAABB(const std::vector<glm::vec3>& verts, glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);
    for (const auto& v : verts) {
        outMin = glm::min(outMin, v);
        outMax = glm::max(outMax, v);
    }
}

/// @brief Try to fit a sphere to the vertices.
/// Returns true if all vertices are within `tolerance` (relative to radius) of
/// a common radius from the centroid.
///
/// Guards against false positives (cubes, planes, etc.) by:
///   - Requiring enough vertices (a sphere mesh has far more than a cube's 8).
///   - Checking that the AABB is roughly cubic (sphere's AABB has equal extents).
///   - Tight tolerance on radius deviation.
bool fitSphere(const std::vector<glm::vec3>& verts, glm::vec3& outCenter, float& outRadius, float tolerance = 0.02f)
{
    // A typical sphere mesh has >=20 vertices; cubes have 8-24.
    // Require enough to distinguish from low-poly non-spheres.
    if (verts.size() < 32)
        return false;

    // AABB must be roughly cubic (all extents within 15% of each other).
    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (const auto& v : verts) {
        bmin = glm::min(bmin, v);
        bmax = glm::max(bmax, v);
    }
    const glm::vec3 ext = bmax - bmin;
    const float maxExt = std::max({ext.x, ext.y, ext.z});
    const float minExt = std::min({ext.x, ext.y, ext.z});
    if (maxExt < 1e-4f)
        return false;
    if (minExt / maxExt < 0.85f)
        return false; // elongated — not a sphere

    // Centroid
    glm::vec3 center = (bmin + bmax) * 0.5f;

    // Mean radius
    float meanR = 0.0f;
    for (const auto& v : verts)
        meanR += glm::length(v - center);
    meanR /= static_cast<float>(verts.size());

    if (meanR < 1e-4f)
        return false;

    // Check deviation — tight tolerance rejects cubes (which have ~15% deviation).
    float maxDev = 0.0f;
    for (const auto& v : verts) {
        const float r = glm::length(v - center);
        maxDev = std::max(maxDev, std::abs(r - meanR) / meanR);
    }

    if (maxDev > tolerance)
        return false;

    outCenter = center;
    outRadius = meanR;
    return true;
}

/// @brief Try to fit a vertical (Y-axis) cylinder to the vertices.
/// Returns true if the XZ projection forms a circle (within tolerance) and the
/// Y range forms a clean vertical extent.
///
/// Guards against false positives by:
///   - Requiring enough vertices (cylinder meshes are typically >=16).
///   - Checking the AABB XZ extents are roughly equal (circular cross-section).
///   - Tight tolerance on XZ radius deviation.
bool fitCylinder(const std::vector<glm::vec3>& verts,
                 glm::vec3& outBase,
                 float& outRadius,
                 float& outHeight,
                 float tolerance = 0.05f)
{
    // A typical cylinder mesh has >=16 vertices; cubes have 8-24.
    if (verts.size() < 16)
        return false;

    // XZ centroid
    float cx = 0.0f, cz = 0.0f;
    float yMin = 1e30f, yMax = -1e30f;
    for (const auto& v : verts) {
        cx += v.x;
        cz += v.z;
        yMin = std::min(yMin, v.y);
        yMax = std::max(yMax, v.y);
    }
    cx /= static_cast<float>(verts.size());
    cz /= static_cast<float>(verts.size());

    const float height = yMax - yMin;
    if (height < 1e-4f)
        return false;

    // XZ bounding extents must be roughly equal (circular cross-section).
    float xMin = 1e30f, xMax = -1e30f, zMin = 1e30f, zMax = -1e30f;
    for (const auto& v : verts) {
        xMin = std::min(xMin, v.x);
        xMax = std::max(xMax, v.x);
        zMin = std::min(zMin, v.z);
        zMax = std::max(zMax, v.z);
    }
    const float xzExtX = xMax - xMin;
    const float xzExtZ = zMax - zMin;
    const float xzMax = std::max(xzExtX, xzExtZ);
    const float xzMin = std::min(xzExtX, xzExtZ);
    if (xzMax < 1e-4f)
        return false;
    if (xzMin / xzMax < 0.80f)
        return false; // rectangular cross-section — not a cylinder

    // Mean XZ radius
    float meanR = 0.0f;
    int countMid = 0;
    for (const auto& v : verts) {
        // Skip vertices at the very top/bottom caps — they might be a single
        // center vertex that would skew the radius measurement.
        const float relY = (v.y - yMin) / height;
        if (relY > 0.05f && relY < 0.95f) {
            meanR += std::sqrt((v.x - cx) * (v.x - cx) + (v.z - cz) * (v.z - cz));
            ++countMid;
        }
    }

    // If most vertices are at caps, measure all of them
    if (countMid < 4) {
        meanR = 0.0f;
        for (const auto& v : verts)
            meanR += std::sqrt((v.x - cx) * (v.x - cx) + (v.z - cz) * (v.z - cz));
        meanR /= static_cast<float>(verts.size());
    } else {
        meanR /= static_cast<float>(countMid);
    }

    if (meanR < 1e-4f)
        return false;

    // Reject if the shape is very flat (disc) — height should be comparable to radius
    // Actually, flat cylinders are still valid collision (e.g. a coin shape), so don't reject.

    // Check that XZ distances are consistent (circle test)
    float maxDev = 0.0f;
    for (const auto& v : verts) {
        const float r = std::sqrt((v.x - cx) * (v.x - cx) + (v.z - cz) * (v.z - cz));
        // Cap vertices at r~0 are fine (center of disc cap)
        if (r > meanR * 0.1f) {
            maxDev = std::max(maxDev, std::abs(r - meanR) / meanR);
        }
    }

    if (maxDev > tolerance)
        return false;

    outBase = glm::vec3(cx, yMin, cz);
    outRadius = meanR;
    outHeight = height;
    return true;
}

/// @brief Check if the mesh is an axis-aligned box (all face normals align with world axes).
bool isAxisAlignedBox(const aiMesh* mesh, const glm::mat4& world)
{
    if (!mesh->HasNormals())
        return false;

    const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(world)));

    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
        const glm::vec3 localN(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
        const glm::vec3 wn = glm::normalize(normalMat * localN);

        // Check if normal aligns with one of the 6 axis directions
        bool aligned = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(std::abs(wn[axis]) - 1.0f) < 0.1f) {
                aligned = true;
                break;
            }
        }
        if (!aligned)
            return false;
    }
    return true;
}

// Convex hull → WorldBrush

/// @brief A face plane with a hash for deduplication.
struct FacePlane
{
    glm::vec3 normal;
    float distance;
};

/// @brief Try to fit a convex hull (`WorldBrush`) to the mesh.
///
/// Returns true if **and only if** the mesh is genuinely convex *and* its
/// unique face planes fit within `WorldBrush::k_maxPlanes`.
///
/// "Genuinely convex" means: for every triangle face, all of the mesh's other
/// vertices lie on or behind that face's plane (using the outward-facing
/// normal).  An L-shape, U-shape, hollow tube, or anything with a concave
/// region will fail this test even if its unique-plane count happens to fit
/// in `k_maxPlanes` — those meshes are silently mis-represented if you build
/// a brush from them, because the brush is the *convex hull* of the planes,
/// not the actual concave shape.
///
/// Outward orientation is auto-detected from the mesh centroid: whichever side
/// of each face the centroid is on is treated as the interior.  This means
/// inverted-winding meshes are handled correctly without a manual flip.
bool extractConvexBrush(const aiMesh* mesh, const glm::mat4& world, float scale, WorldBrush& outBrush)
{
    if (!mesh->HasPositions() || mesh->mNumFaces == 0)
        return false;

    // World-space vertex cache — we'll project every vertex against every
    // face plane below, so do the transform once.
    const std::vector<glm::vec3> verts = getWorldVertices(mesh, world, scale);
    if (verts.size() < 4)
        return false; // a closed convex volume needs at least a tetrahedron

    // Mesh extent — used to scale a relative tolerance for "on the plane".
    glm::vec3 bmin, bmax;
    computeAABB(verts, bmin, bmax);
    const glm::vec3 centroid = (bmin + bmax) * 0.5f;
    const glm::vec3 ext = bmax - bmin;
    const float meshDiag = glm::length(ext);
    if (meshDiag < 1e-6f)
        return false; // degenerate (zero-extent mesh)

    // 0.1% of the mesh diagonal — generous enough to absorb numerical noise
    // and Blender's typical export precision, tight enough to flag real
    // concavities.  For a 200-unit cube that's 0.2 units, well below any
    // "real" feature size.
    const float tolerance = meshDiag * 0.001f;

    std::vector<FacePlane> uniquePlanes;

    for (unsigned int fi = 0; fi < mesh->mNumFaces; ++fi) {
        const aiFace& face = mesh->mFaces[fi];
        if (face.mNumIndices < 3)
            continue;

        const glm::vec3& wp0 = verts[face.mIndices[0]];
        const glm::vec3& wp1 = verts[face.mIndices[1]];
        const glm::vec3& wp2 = verts[face.mIndices[2]];

        glm::vec3 faceN = glm::cross(wp1 - wp0, wp2 - wp0);
        const float len = glm::length(faceN);
        if (len < 1e-8f)
            continue; // sliver / degenerate triangle — skip
        faceN /= len;
        float dist = glm::dot(faceN, wp0);

        // Pick the outward orientation: the centroid should be on the *inside*
        // (negative side) of each face plane for a convex closed mesh.  If the
        // raw cross-product points the wrong way (inward winding), flip both
        // normal and distance so the plane is consistently outward.
        const float centroidSide = glm::dot(faceN, centroid) - dist;
        if (centroidSide > tolerance) {
            faceN = -faceN;
            dist = -dist;
        }

        // Convexity verification: every vertex in the mesh must be on or
        // behind this plane.  A single vertex on the *outside* (positive side
        // beyond tolerance) means the mesh has a concavity here — treating it
        // as a brush would silently give the player the wrong collision (the
        // player would collide with the convex hull instead of the real
        // surface).  Bail out so the caller falls through to triMesh.
        for (const auto& v : verts) {
            const float d = glm::dot(faceN, v) - dist;
            if (d > tolerance)
                return false;
        }

        // Deduplicate by normal direction (parallel coplanar tris share a plane).
        bool duplicate = false;
        for (const auto& existing : uniquePlanes) {
            if (glm::dot(faceN, existing.normal) > 0.999f) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            uniquePlanes.push_back({faceN, dist});
    }

    // A closed convex volume needs at least 4 planes (tetrahedron); fewer means
    // an unbounded half-space.  And it must fit within the brush's plane budget.
    if (static_cast<int>(uniquePlanes.size()) < 4 || static_cast<int>(uniquePlanes.size()) > WorldBrush::k_maxPlanes)
        return false;

    // Build the brush.
    outBrush.planeCount = static_cast<int>(uniquePlanes.size());
    for (int i = 0; i < outBrush.planeCount; ++i) {
        outBrush.planes[i].normal = uniquePlanes[static_cast<size_t>(i)].normal;
        outBrush.planes[i].distance = uniquePlanes[static_cast<size_t>(i)].distance;
    }

    return true;
}

// Convex decomposition (V-HACD)

/// @brief Convert one V-HACD output hull to a `WorldBrush`.
///
/// V-HACD hands us each hull as a triangle mesh (vertices + indices in double
/// precision).  We extract its unique face planes (deduplicating coplanar
/// triangles), auto-detect the outward orientation from the centroid, and
/// pack them into a brush.  Returns false if the hull is degenerate or its
/// plane count exceeds the brush budget.
bool buildBrushFromHull(const VHACD::IVHACD::ConvexHull& hull, WorldBrush& outBrush)
{
    if (hull.m_points.empty() || hull.m_triangles.empty())
        return false;

    // Convert hull vertices from V-HACD's double-precision to glm::vec3.
    std::vector<glm::vec3> verts;
    verts.reserve(hull.m_points.size());
    for (const auto& p : hull.m_points)
        verts.emplace_back(static_cast<float>(p.mX), static_cast<float>(p.mY), static_cast<float>(p.mZ));

    // Centroid + tolerance for outward-orientation detection (mirrors the
    // logic in extractConvexBrush so we keep behaviour consistent).
    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (const auto& v : verts) {
        bmin = glm::min(bmin, v);
        bmax = glm::max(bmax, v);
    }
    const glm::vec3 centroid = (bmin + bmax) * 0.5f;
    const float meshDiag = glm::length(bmax - bmin);
    if (meshDiag < 1e-6f)
        return false;
    const float tolerance = meshDiag * 0.001f;

    std::vector<FacePlane> uniquePlanes;
    uniquePlanes.reserve(hull.m_triangles.size());

    for (const auto& tri : hull.m_triangles) {
        if (tri.mI0 >= verts.size() || tri.mI1 >= verts.size() || tri.mI2 >= verts.size())
            continue;

        const glm::vec3& v0 = verts[tri.mI0];
        const glm::vec3& v1 = verts[tri.mI1];
        const glm::vec3& v2 = verts[tri.mI2];

        glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
        const float len = glm::length(n);
        if (len < 1e-8f)
            continue; // sliver
        n /= len;
        float dist = glm::dot(n, v0);

        // Outward orientation: centroid should be on the inside (negative side).
        if (glm::dot(n, centroid) - dist > tolerance) {
            n = -n;
            dist = -dist;
        }

        // Deduplicate by normal direction (coplanar tris share a plane).
        bool dup = false;
        for (const auto& existing : uniquePlanes) {
            if (glm::dot(n, existing.normal) > 0.999f) {
                dup = true;
                break;
            }
        }
        if (!dup)
            uniquePlanes.push_back({n, dist});
    }

    if (static_cast<int>(uniquePlanes.size()) < 4 || static_cast<int>(uniquePlanes.size()) > WorldBrush::k_maxPlanes)
        return false;

    outBrush.planeCount = static_cast<int>(uniquePlanes.size());
    for (int i = 0; i < outBrush.planeCount; ++i) {
        outBrush.planes[i].normal = uniquePlanes[static_cast<size_t>(i)].normal;
        outBrush.planes[i].distance = uniquePlanes[static_cast<size_t>(i)].distance;
    }
    return true;
}

/// @brief Run V-HACD convex decomposition on a non-convex mesh and append the
///        resulting hulls to `out.brushes`.
///
/// Tries V-HACD's three fill modes in order:
///   1. `FLOOD_FILL`     — closed solid meshes (default).  Voxelises the
///                         interior via flood-fill from outside.  Best
///                         quality for watertight solids.
///   2. `RAYCAST_FILL`   — meshes with small holes.  Determines inside vs
///                         outside by raycasting around each voxel.  More
///                         robust than flood-fill against leaks.
///   3. `SURFACE_ONLY`   — hollow shells (e.g. a tube wall, a curved
///                         walkable corridor).  Treats the mesh as a thin
///                         skin and produces hulls along the surface only,
///                         so the cavity stays empty and walkable.
///
/// We accept the *first* mode that produces ≥1 successfully-converted hull —
/// the modes are ordered "most likely correct for solid meshes" first so
/// that solid non-convex shapes (chairs, L-blocks) don't accidentally get
/// the surface-only treatment.
///
/// @return Number of brushes appended to `out.brushes`. Zero on failure
/// (caller should fall through to triMesh).
size_t decomposeIntoBrushes(
    const aiMesh* mesh, const glm::mat4& world, float scale, const char* nodeName, MapCollisionData& out)
{
    if (!mesh->HasPositions() || mesh->mNumFaces == 0)
        return 0;

    // Build float vertex array (world-space) and uint32 index array.
    const std::vector<glm::vec3> wverts = getWorldVertices(mesh, world, scale);
    if (wverts.size() < 4)
        return 0;

    std::vector<float> points;
    points.reserve(wverts.size() * 3);
    for (const auto& v : wverts) {
        points.push_back(v.x);
        points.push_back(v.y);
        points.push_back(v.z);
    }

    std::vector<uint32_t> indices;
    indices.reserve(mesh->mNumFaces * 3);
    for (unsigned int fi = 0; fi < mesh->mNumFaces; ++fi) {
        const aiFace& face = mesh->mFaces[fi];
        if (face.mNumIndices != 3)
            continue;
        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }
    if (indices.size() < 9)
        return 0;

    // Try each fill mode until one yields hulls.
    //
    // FLOOD_FILL is tried first because it's the most efficient for the common
    // case (closed solid mesh, e.g. a chair, an L-shaped block): it voxelises
    // the volume, flood-fills "outside" from the bounds, and decomposes the
    // remaining "inside" into a small number of compact hulls.
    //
    // RAYCAST_FILL is a fallback for meshes with small holes that defeat the
    // flood-fill (it decides inside-vs-outside per voxel via raycasts).
    //
    // SURFACE_ONLY is the last resort for thin-shell meshes that have no
    // enclosed volume (e.g. a single-layer bezier tube the artist intended to
    // be walkable from inside).  It decomposes only the surface skin so the
    // cavity stays empty.  If you have a hollow-walkable mesh that's getting
    // its cavity filled by FLOOD_FILL, give the wall thickness in Blender
    // (Solidify modifier) — or move SURFACE_ONLY to the front of this list.
    static constexpr struct
    {
        VHACD::FillMode mode;
        const char* name;
    } k_fillModes[] = {
        {VHACD::FillMode::FLOOD_FILL, "flood-fill"},
        {VHACD::FillMode::RAYCAST_FILL, "raycast-fill"},
        {VHACD::FillMode::SURFACE_ONLY, "surface-only"},
    };

    for (const auto& mode : k_fillModes) {
        VHACD::IVHACD::Parameters params;
        params.m_maxConvexHulls = 32;      // total hull budget per mesh
        params.m_resolution = 100000;      // voxel grid (default 400000 is slower)
        params.m_maxNumVerticesPerCH = 32; // bounds plane count per hull (after dedup)
        params.m_asyncACD = false;         // synchronous: we're at load-time
        params.m_fillMode = mode.mode;

        VHACD::IVHACD* iface = VHACD::CreateVHACD();
        const bool computed = iface->Compute(points.data(),
                                             static_cast<uint32_t>(points.size() / 3),
                                             indices.data(),
                                             static_cast<uint32_t>(indices.size() / 3),
                                             params);

        const uint32_t hullCount = computed ? iface->GetNConvexHulls() : 0u;
        if (hullCount == 0) {
            iface->Release();
            continue; // try next fill mode
        }

        size_t addedBrushes = 0;
        for (uint32_t i = 0; i < hullCount; ++i) {
            VHACD::IVHACD::ConvexHull hull;
            if (!iface->GetConvexHull(i, hull))
                continue;

            WorldBrush brush{};
            if (buildBrushFromHull(hull, brush)) {
                out.brushes.push_back(brush);
                ++addedBrushes;
            }
        }
        iface->Release();

        if (addedBrushes > 0) {
            SDL_Log(
                "MapLoader: V-HACD decomposed '%s' into %zu brush(es) [mode=%s]", nodeName, addedBrushes, mode.name);
            return addedBrushes;
        }
    }

    SDL_Log("MapLoader: V-HACD failed for '%s' (all fill modes produced 0 hulls); falling back to triMesh", nodeName);
    return 0;
}

// Triangle mesh construction

/// @brief Build a WorldTriMesh from an Assimp mesh.
void buildTriMeshFromAiMesh(const aiMesh* mesh, const glm::mat4& world, float scale, WorldTriMesh& out)
{
    out.vertices.reserve(mesh->mNumVertices);
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
        const glm::vec4 local(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f);
        out.vertices.push_back(glm::vec3(world * local) * scale);
    }
    for (unsigned int fi = 0; fi < mesh->mNumFaces; ++fi) {
        const aiFace& face = mesh->mFaces[fi];
        if (face.mNumIndices != 3)
            continue;
        out.indices.push_back(face.mIndices[0]);
        out.indices.push_back(face.mIndices[1]);
        out.indices.push_back(face.mIndices[2]);
    }
    computeAABB(out.vertices, out.boundsMin, out.boundsMax);
}

// Per-mesh collision extraction with auto-detection

/// @brief Check if a node name belongs to Blender default scene objects that
///        should never generate collision (armatures, cameras, lights, mannequins).
bool shouldSkipNode(const char* nodeName)
{
    const std::string name(nodeName);
    // Blender's default mannequin meshes.
    if (containsCI(name, "Beta_"))
        return true;
    // Blender default objects that sometimes leak into exports.
    if (containsCI(name, "Armature") || containsCI(name, "Camera") || containsCI(name, "Light"))
        return true;
    return false;
}

/// @brief Determine the best collision primitive for a mesh and add it to `out`.
/// @param decomposeNonConvex  If true and the mesh fails the single-hull
///        convex-brush check, run V-HACD convex decomposition before falling
///        back to triMesh.
void extractMeshCollision(const aiMesh* mesh,
                          const glm::mat4& world,
                          float scale,
                          const std::string& forceType,
                          const char* nodeName,
                          bool decomposeNonConvex,
                          MapCollisionData& out)
{
    if (!mesh->HasPositions() || mesh->mNumVertices == 0)
        return;

    // Skip known non-geometry objects (Blender mannequin, armatures, etc.)
    if (forceType.empty() && shouldSkipNode(nodeName)) {
        SDL_Log("MapLoader: skipping non-geometry node '%s'", nodeName);
        return;
    }

    const std::vector<glm::vec3> verts = getWorldVertices(mesh, world, scale);
    if (verts.empty())
        return;

    // --- Forced type from sub-collection ---
    if (forceType == "boxes") {
        glm::vec3 bmin, bmax;
        computeAABB(verts, bmin, bmax);
        out.boxes.push_back({bmin, bmax});
        SDL_Log("MapLoader: AABB (forced) [%.1f,%.1f,%.1f]→[%.1f,%.1f,%.1f] '%s'",
                static_cast<double>(bmin.x),
                static_cast<double>(bmin.y),
                static_cast<double>(bmin.z),
                static_cast<double>(bmax.x),
                static_cast<double>(bmax.y),
                static_cast<double>(bmax.z),
                nodeName);
        return;
    }
    if (forceType == "spheres") {
        glm::vec3 center;
        float radius;
        if (fitSphere(verts, center, radius)) {
            out.spheres.push_back({center, radius});
        } else {
            // Fallback: bounding sphere
            glm::vec3 bmin, bmax;
            computeAABB(verts, bmin, bmax);
            center = (bmin + bmax) * 0.5f;
            radius = glm::length(bmax - bmin) * 0.5f;
            out.spheres.push_back({center, radius});
        }
        SDL_Log("MapLoader: Sphere (forced) c=(%.1f,%.1f,%.1f) r=%.1f '%s'",
                static_cast<double>(out.spheres.back().center.x),
                static_cast<double>(out.spheres.back().center.y),
                static_cast<double>(out.spheres.back().center.z),
                static_cast<double>(out.spheres.back().radius),
                nodeName);
        return;
    }
    if (forceType == "cylinders") {
        glm::vec3 base;
        float radius, height;
        if (fitCylinder(verts, base, radius, height)) {
            out.cylinders.push_back({base, radius, height});
        } else {
            // Fallback: bounding cylinder from AABB
            glm::vec3 bmin, bmax;
            computeAABB(verts, bmin, bmax);
            base = glm::vec3((bmin.x + bmax.x) * 0.5f, bmin.y, (bmin.z + bmax.z) * 0.5f);
            radius = std::max(bmax.x - bmin.x, bmax.z - bmin.z) * 0.5f;
            height = bmax.y - bmin.y;
            out.cylinders.push_back({base, radius, height});
        }
        SDL_Log("MapLoader: Cylinder (forced) base=(%.1f,%.1f,%.1f) r=%.1f h=%.1f '%s'",
                static_cast<double>(out.cylinders.back().base.x),
                static_cast<double>(out.cylinders.back().base.y),
                static_cast<double>(out.cylinders.back().base.z),
                static_cast<double>(out.cylinders.back().radius),
                static_cast<double>(out.cylinders.back().height),
                nodeName);
        return;
    }
    if (forceType == "brushes") {
        WorldBrush brush{};
        if (extractConvexBrush(mesh, world, scale, brush)) {
            out.brushes.push_back(brush);
            SDL_Log("MapLoader: Brush (forced) %d planes '%s'", brush.planeCount, nodeName);
        } else {
            // Fallback to AABB
            glm::vec3 bmin, bmax;
            computeAABB(verts, bmin, bmax);
            out.boxes.push_back({bmin, bmax});
            SDL_Log("MapLoader: AABB (brush fallback) '%s'", nodeName);
        }
        return;
    }
    if (forceType == "meshes") {
        WorldTriMesh tm;
        buildTriMeshFromAiMesh(mesh, world, scale, tm);
        buildTriMeshBVH(tm);
        SDL_Log("MapLoader: TriMesh (forced) %zu tris, %zu BVH nodes '%s'",
                tm.indices.size() / 3,
                tm.bvhNodes.size(),
                nodeName);
        out.triMeshes.push_back(std::move(tm));
        return;
    }

    // --- Auto-detection ---
    // Order: AABB → cylinder → sphere → brush → fallback.
    // Cylinder before sphere because a cylinder whose height ≈ diameter has a
    // roughly cubic AABB and near-equal vertex radii — it would pass the sphere
    // test.  Cylinder is the more specific check (requires circular XZ cross-section
    // AND vertical extent), so it runs first.

    // 1. Try axis-aligned box (cheapest primitive, catches most map geometry)
    if (isAxisAlignedBox(mesh, world)) {
        glm::vec3 bmin, bmax;
        computeAABB(verts, bmin, bmax);
        out.boxes.push_back({bmin, bmax});
        SDL_Log("MapLoader: AABB (auto) [%.1f,%.1f,%.1f]→[%.1f,%.1f,%.1f] '%s'",
                static_cast<double>(bmin.x),
                static_cast<double>(bmin.y),
                static_cast<double>(bmin.z),
                static_cast<double>(bmax.x),
                static_cast<double>(bmax.y),
                static_cast<double>(bmax.z),
                nodeName);
        return;
    }

    // 2. Try cylinder (before sphere — cylinder is more specific)
    {
        glm::vec3 base;
        float radius, height;
        if (fitCylinder(verts, base, radius, height)) {
            out.cylinders.push_back({base, radius, height});
            SDL_Log("MapLoader: Cylinder (auto) base=(%.1f,%.1f,%.1f) r=%.1f h=%.1f '%s'",
                    static_cast<double>(base.x),
                    static_cast<double>(base.y),
                    static_cast<double>(base.z),
                    static_cast<double>(radius),
                    static_cast<double>(height),
                    nodeName);
            return;
        }
    }

    // 3. Try sphere (only if cylinder test already rejected it)
    {
        glm::vec3 center;
        float radius;
        if (fitSphere(verts, center, radius)) {
            out.spheres.push_back({center, radius});
            SDL_Log("MapLoader: Sphere (auto) c=(%.1f,%.1f,%.1f) r=%.1f '%s'",
                    static_cast<double>(center.x),
                    static_cast<double>(center.y),
                    static_cast<double>(center.z),
                    static_cast<double>(radius),
                    nodeName);
            return;
        }
    }

    // 4. Try a single convex brush (for genuinely-convex meshes that fit in
    //    one hull within k_maxPlanes — cheapest collision shape after AABB).
    {
        WorldBrush brush{};
        if (extractConvexBrush(mesh, world, scale, brush)) {
            out.brushes.push_back(brush);
            SDL_Log("MapLoader: Brush (auto) %d planes '%s'", brush.planeCount, nodeName);
            return;
        }
    }

    // 4.5. Try V-HACD convex decomposition: split the non-convex mesh into a
    //     handful of convex brushes.  Smoother runtime collision than triMesh
    //     (no per-triangle MTV jitter on curved surfaces) and the cost is paid
    //     only once at load time.
    if (decomposeNonConvex && decomposeIntoBrushes(mesh, world, scale, nodeName, out) > 0) {
        return;
    }

    // 5. TriMesh (for complex geometry that doesn't fit simpler primitives)
    if (mesh->mNumFaces >= 2) {
        WorldTriMesh tm;
        buildTriMeshFromAiMesh(mesh, world, scale, tm);
        if (tm.indices.size() >= 3) {
            buildTriMeshBVH(tm);
            SDL_Log("MapLoader: TriMesh (auto) %zu tris, %zu BVH nodes '%s'",
                    tm.indices.size() / 3,
                    tm.bvhNodes.size(),
                    nodeName);
            out.triMeshes.push_back(std::move(tm));
            return;
        }
    }

    // 6. Final fallback: AABB (degenerate mesh)
    {
        glm::vec3 bmin, bmax;
        computeAABB(verts, bmin, bmax);
        out.boxes.push_back({bmin, bmax});
        SDL_Log("MapLoader: AABB (fallback) [%.1f,%.1f,%.1f]→[%.1f,%.1f,%.1f] '%s'",
                static_cast<double>(bmin.x),
                static_cast<double>(bmin.y),
                static_cast<double>(bmin.z),
                static_cast<double>(bmax.x),
                static_cast<double>(bmax.y),
                static_cast<double>(bmax.z),
                nodeName);
    }
}

// Scene graph traversal

void extractCollision(const aiNode* node,
                      const aiScene* scene,
                      const std::string& collectionName,
                      bool allAreCollision,
                      bool guessShapesProcessed,
                      bool decomposeNonConvex,
                      float scale,
                      MapCollisionData& out)
{
    const bool isCollision = allAreCollision || isUnderCollectionNode(node, collectionName) ||
                             containsCI(std::string(node->mName.C_Str()), collectionName);

    if (isCollision) {
        const glm::mat4 world = accumulatedTransform(node);

        // Pick the shape strategy:
        //   - Prototype mode (allAreCollision): no forcing — let auto-detection
        //     pick the best primitive for every mesh.
        //   - Separated mode WITHOUT shape-guessing (default): force every
        //     collision mesh to a raw triangle mesh, preserving the exact
        //     geometry the artist authored in Blender.
        //   - Separated mode WITH shape-guessing: respect sub-collection
        //     naming ("Boxes/", "Cylinders/") and Blender primitive names,
        //     falling back to auto-detection.
        std::string forceType;
        if (!allAreCollision) {
            forceType = guessShapesProcessed ? getCollectionType(node) : std::string("meshes");
        }

        // V-HACD only fires in separated + shape-guessing mode and only on
        // meshes that fall through to auto-detection (no forced type).  In
        // prototype mode every mesh is collision (often hundreds), and forced
        // triMesh / brush requests should be honoured exactly.
        const bool tryDecompose = decomposeNonConvex && !allAreCollision && guessShapesProcessed && forceType.empty();

        for (unsigned int mi = 0; mi < node->mNumMeshes; ++mi) {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
            extractMeshCollision(mesh, world, scale, forceType, node->mName.C_Str(), tryDecompose, out);
        }
    }

    for (unsigned int c = 0; c < node->mNumChildren; ++c)
        extractCollision(node->mChildren[c],
                         scene,
                         collectionName,
                         allAreCollision,
                         guessShapesProcessed,
                         decomposeNonConvex,
                         scale,
                         out);
}

} // namespace

// Public API

bool loadMapCollision(const std::string& path, MapCollisionData& out, const MapLoadOptions& opts)
{
    Assimp::Importer importer;

    const auto flags =
        static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals);

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SDL_Log("MapLoader: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    out.planes.clear();
    out.boxes.clear();
    out.brushes.clear();
    out.cylinders.clear();
    out.spheres.clear();
    out.triMeshes.clear();

    extractCollision(scene->mRootNode,
                     scene,
                     opts.collisionCollection,
                     opts.allMeshesAreCollision,
                     opts.guessShapesProcessed,
                     opts.decomposeNonConvex,
                     opts.scale,
                     out);

    const size_t total =
        out.boxes.size() + out.brushes.size() + out.cylinders.size() + out.spheres.size() + out.triMeshes.size();
    if (total == 0) {
        SDL_Log("MapLoader: WARNING — no collision geometry extracted from '%s'", path.c_str());
    }

    // Optionally add an infinite floor plane at the lowest Y across all geometry.
    if (opts.addFloorPlane && total > 0) {
        float lowestY = 1e30f;
        for (const auto& b : out.boxes)
            lowestY = std::min(lowestY, b.min.y);
        for (const auto& c : out.cylinders)
            lowestY = std::min(lowestY, c.base.y);
        for (const auto& s : out.spheres)
            lowestY = std::min(lowestY, s.center.y - s.radius);
        for (const auto& tm : out.triMeshes)
            lowestY = std::min(lowestY, tm.boundsMin.y);

        out.planes.push_back(Plane{.normal = {0.0f, 1.0f, 0.0f}, .distance = lowestY});
        SDL_Log("MapLoader: added floor plane at y=%.1f", static_cast<double>(lowestY));
    }

    SDL_Log("MapLoader: loaded '%s' — %zu plane(s), %zu box(es), %zu brush(es), %zu cylinder(s), %zu sphere(s), %zu "
            "trimesh(es)",
            path.c_str(),
            out.planes.size(),
            out.boxes.size(),
            out.brushes.size(),
            out.cylinders.size(),
            out.spheres.size(),
            out.triMeshes.size());

    return true;
}

bool loadPropCollision(const std::string& path, MapCollisionData& out, glm::vec3 position, float scale)
{
    Assimp::Importer importer;

    const auto flags =
        static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals);

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SDL_Log("MapLoader: failed to load prop '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    // Build the prop transform: translate to world position, then uniform scale.
    const glm::mat4 propTransform = glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(scale));

    // Walk the scene graph — treat all meshes as collision (like prototype mode),
    // but bake the prop's world position into the transform.
    const auto prevBoxes = out.boxes.size();
    const auto prevCyls = out.cylinders.size();
    const auto prevSpheres = out.spheres.size();
    const auto prevBrushes = out.brushes.size();
    const auto prevTri = out.triMeshes.size();

    // We can't use extractCollision directly because it computes the transform
    // from the node hierarchy.  Instead, use a simple recursive walk that
    // multiplies our prop transform with each node's accumulated transform.
    struct Walker
    {
        static void
        walk(const aiNode* node, const aiScene* scene, const glm::mat4& parentTransform, MapCollisionData& out)
        {
            const glm::mat4 world = parentTransform * aiToGlm(node->mTransformation);

            for (unsigned int mi = 0; mi < node->mNumMeshes; ++mi) {
                const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
                // Scale = 1.0 because the scale is already baked into the transform.
                // decomposeNonConvex = false: props are loaded one-shot per call
                // and a typical prop GLB has many meshes, V-HACD on each at
                // load time would slow startup unacceptably.  Props use the
                // existing primitive auto-detection + triMesh fallback.
                extractMeshCollision(mesh,
                                     world,
                                     1.0f,
                                     "",
                                     node->mName.C_Str(),
                                     /*decomposeNonConvex=*/false,
                                     out);
            }

            for (unsigned int c = 0; c < node->mNumChildren; ++c)
                walk(node->mChildren[c], scene, world, out);
        }
    };

    Walker::walk(scene->mRootNode, scene, propTransform, out);

    const auto newBoxes = out.boxes.size() - prevBoxes;
    const auto newCyls = out.cylinders.size() - prevCyls;
    const auto newSpheres = out.spheres.size() - prevSpheres;
    const auto newBrushes = out.brushes.size() - prevBrushes;
    const auto newTri = out.triMeshes.size() - prevTri;

    SDL_Log("MapLoader: prop '%s' at (%.0f,%.0f,%.0f) scale=%.1f — +%zu box, +%zu cyl, +%zu sph, +%zu brush, +%zu tri",
            path.c_str(),
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z),
            static_cast<double>(scale),
            newBoxes,
            newCyls,
            newSpheres,
            newBrushes,
            newTri);

    return true;
}

} // namespace physics
