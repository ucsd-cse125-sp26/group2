/// @file CookedMeshFormat.cpp
/// @brief Serialization + deserialization of cooked trimesh blobs.

#include "ecs/physics/CookedMeshFormat.hpp"

#include <cstring>
#include <fstream>

namespace physics::cook
{

namespace
{

template <typename T>
void appendRaw(std::vector<uint8_t>& out, const T& value)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
void appendArray(std::vector<uint8_t>& out, const T* data, size_t count)
{
    if (count == 0u)
        return;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + count * sizeof(T));
}

template <typename T>
bool readRaw(std::span<const uint8_t>& cursor, T& out)
{
    if (cursor.size() < sizeof(T))
        return false;
    std::memcpy(&out, cursor.data(), sizeof(T));
    cursor = cursor.subspan(sizeof(T));
    return true;
}

template <typename T>
bool readArray(std::span<const uint8_t>& cursor, std::vector<T>& out, size_t count)
{
    if (count == 0u) {
        out.clear();
        return true;
    }
    const size_t bytes = count * sizeof(T);
    if (cursor.size() < bytes)
        return false;
    out.resize(count);
    std::memcpy(out.data(), cursor.data(), bytes);
    cursor = cursor.subspan(bytes);
    return true;
}

} // namespace

std::vector<uint8_t> serialize(const WorldTriMesh& mesh)
{
    std::vector<uint8_t> blob;
    const uint32_t triCount = static_cast<uint32_t>(mesh.indices.size() / 3u);
    const uint32_t vertCount = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t bvhNodeCount = static_cast<uint32_t>(mesh.bvhNodes.size());

    // Reserve a generous upper bound to avoid reallocs.
    blob.reserve(sizeof(Header) + vertCount * sizeof(glm::vec3) + triCount * (sizeof(uint32_t) * 3 + sizeof(glm::vec3) + 3) +
                 bvhNodeCount * sizeof(BVHNode));

    Header h;
    h.magic = k_magic;
    h.version = k_version;
    h.triCount = triCount;
    h.vertCount = vertCount;
    h.bvhNodeCount = bvhNodeCount;
    h.defaultSurface = static_cast<uint32_t>(mesh.defaultSurface);
    h.boundsMin = mesh.boundsMin;
    h.boundsMax = mesh.boundsMax;
    appendRaw(blob, h);

    appendArray(blob, mesh.vertices.data(), vertCount);
    appendArray(blob, mesh.indices.data(), mesh.indices.size());

    appendArray(blob, mesh.faceNormals.data(), mesh.faceNormals.size());
    appendArray(blob, mesh.edgeActive.data(), mesh.edgeActive.size());
    appendArray(blob, mesh.vertActive.data(), mesh.vertActive.size());

    // Triangle materials: serialize the whole vector (caller decides size).
    // Empty vector means "use defaultSurface for every triangle"; we encode
    // the size as a uint32 prefix to disambiguate.
    const uint32_t matCount = static_cast<uint32_t>(mesh.triangleMaterials.size());
    appendRaw(blob, matCount);
    appendArray(blob, mesh.triangleMaterials.data(), matCount);

    // BVH nodes — serialized as a raw byte run.  `BVHNode` is a POD with
    // no padding-sensitive fields (vec3 / int).
    appendArray(blob, mesh.bvhNodes.data(), bvhNodeCount);
    appendArray(blob, mesh.triIndices.data(), mesh.triIndices.size());

    return blob;
}

bool deserialize(std::span<const uint8_t> blob, WorldTriMesh& out)
{
    out = WorldTriMesh{};

    Header h;
    if (!readRaw(blob, h))
        return false;
    if (h.magic != k_magic || h.version != k_version)
        return false;

    out.boundsMin = h.boundsMin;
    out.boundsMax = h.boundsMax;
    out.defaultSurface = static_cast<SurfaceType>(h.defaultSurface);

    if (!readArray(blob, out.vertices, h.vertCount))
        return false;
    if (!readArray(blob, out.indices, static_cast<size_t>(h.triCount) * 3u))
        return false;
    if (!readArray(blob, out.faceNormals, h.triCount))
        return false;
    if (!readArray(blob, out.edgeActive, h.triCount))
        return false;
    if (!readArray(blob, out.vertActive, h.triCount))
        return false;

    uint32_t matCount = 0;
    if (!readRaw(blob, matCount))
        return false;
    if (!readArray(blob, out.triangleMaterials, matCount))
        return false;

    if (!readArray(blob, out.bvhNodes, h.bvhNodeCount))
        return false;
    if (!readArray(blob, out.triIndices, h.triCount))
        return false;

    return true;
}

bool writeToFile(std::string_view path, const WorldTriMesh& mesh)
{
    const std::vector<uint8_t> blob = serialize(mesh);
    std::ofstream f(std::string{path}, std::ios::binary);
    if (!f.is_open())
        return false;
    f.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    return static_cast<bool>(f);
}

bool readFromFile(std::string_view path, WorldTriMesh& out)
{
    std::ifstream f(std::string{path}, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return false;
    const std::streamsize sz = f.tellg();
    if (sz <= 0)
        return false;
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(buf.data()), sz))
        return false;
    return deserialize(buf, out);
}

} // namespace physics::cook
