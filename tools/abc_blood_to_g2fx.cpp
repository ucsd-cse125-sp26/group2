/// @file abc_blood_to_g2fx.cpp
/// @brief Bake a Pixel Lab Alembic blood hit mesh into Group2 flipbook and volume caches.

#include <Alembic/AbcCoreFactory/All.h>
#include <Alembic/AbcGeom/All.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr uint32_t k_payloadR16FloatNormalized = 1u;
constexpr uint32_t k_flipPayloadRgba16FloatPremul = 1u;

#pragma pack(push, 1)
struct G2VolHeader
{
    char magic[8];
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t frameCount;
    float fps;
    int32_t bboxMin[3];
    int32_t bboxMax[3];
    float voxelSize[3];
    float valueMax;
    float valueP995;
    uint32_t payloadFormat;
    uint32_t headerSize;
    uint64_t payloadBytes;
    uint8_t reserved[164];
};
#pragma pack(pop)

static_assert(sizeof(G2VolHeader) == 256);

#pragma pack(push, 1)
struct G2FlipHeader
{
    char magic[8];
    uint32_t version;
    uint32_t tileWidth;
    uint32_t tileHeight;
    uint32_t atlasWidth;
    uint32_t atlasHeight;
    uint32_t columns;
    uint32_t rows;
    uint32_t frameCount;
    float fps;
    float worldWidth;
    float worldHeight;
    uint32_t payloadFormat;
    uint32_t headerSize;
    uint64_t payloadBytes;
    uint8_t reserved[60];
};
#pragma pack(pop)

static_assert(sizeof(G2FlipHeader) == 128);

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Box3
{
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 max{
        -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
};

struct Color3
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct Pixel
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

struct FrameMesh
{
    std::vector<Vec3> positions;
    std::vector<int32_t> indices;
    std::vector<int32_t> counts;
    std::vector<float> colorBlend;
};

struct Args
{
    fs::path inputPath;
    fs::path flipbookPath;
    fs::path volumePath;
    float fps = 24.0f;
    uint32_t flipbookSize = 384;
    uint32_t volumeMaxDim = 96;
    float padding = 0.18f;
};

uint16_t floatToHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;

    if (exp <= 0) {
        if (exp < -10)
            return static_cast<uint16_t>(sign);
        mantissa = (mantissa | 0x800000u) >> static_cast<uint32_t>(1 - exp);
        return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
    }

    if (exp >= 31)
        return static_cast<uint16_t>(sign | 0x7c00u);

    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | ((mantissa + 0x1000u) >> 13));
}

Args parseArgs(int argc, char** argv)
{
    if (argc < 4) {
        throw std::runtime_error("usage: abc_blood_to_g2fx <input.abc> <output.g2flip> <output.g2vol> "
                                 "[--fps N] [--flipbook-size N] [--volume-max-dim N] [--padding N]");
    }

    Args args;
    args.inputPath = argv[1];
    args.flipbookPath = argv[2];
    args.volumePath = argv[3];

    for (int i = 4; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--fps" && i + 1 < argc) {
            args.fps = std::stof(argv[++i]);
        } else if (arg == "--flipbook-size" && i + 1 < argc) {
            args.flipbookSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--volume-max-dim" && i + 1 < argc) {
            args.volumeMaxDim = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--padding" && i + 1 < argc) {
            args.padding = std::stof(argv[++i]);
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + std::string(arg));
        }
    }

    if (args.fps <= 0.0f)
        throw std::runtime_error("--fps must be positive");
    if (args.flipbookSize == 0)
        throw std::runtime_error("--flipbook-size must be positive");
    if (args.volumeMaxDim < 16)
        throw std::runtime_error("--volume-max-dim must be at least 16");
    if (args.padding < 0.0f || args.padding > 0.75f)
        throw std::runtime_error("--padding must be in the range [0, 0.75]");
    return args;
}

void expand(Box3& box, const Vec3& p)
{
    box.min.x = std::min(box.min.x, p.x);
    box.min.y = std::min(box.min.y, p.y);
    box.min.z = std::min(box.min.z, p.z);
    box.max.x = std::max(box.max.x, p.x);
    box.max.y = std::max(box.max.y, p.y);
    box.max.z = std::max(box.max.z, p.z);
}

Vec3 extent(const Box3& box)
{
    return Vec3{std::max(box.max.x - box.min.x, 1e-4f),
                std::max(box.max.y - box.min.y, 1e-4f),
                std::max(box.max.z - box.min.z, 1e-4f)};
}

Color3 mixColor(const Color3& a, const Color3& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return Color3{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

void blendPixel(std::vector<Pixel>& pixels, uint32_t width, int x, int y, const Color3& color, float alpha)
{
    if (x < 0 || y < 0 || x >= static_cast<int>(width))
        return;
    const uint32_t height = static_cast<uint32_t>(pixels.size() / width);
    if (y >= static_cast<int>(height))
        return;

    alpha = std::clamp(alpha, 0.0f, 1.0f);
    Pixel& dst = pixels[static_cast<size_t>(static_cast<uint32_t>(y) * width + static_cast<uint32_t>(x))];
    const float oneMinusA = 1.0f - dst.a;
    dst.r += oneMinusA * color.r * alpha;
    dst.g += oneMinusA * color.g * alpha;
    dst.b += oneMinusA * color.b * alpha;
    dst.a += oneMinusA * alpha;
}

float edgeFunction(const Vec2& a, const Vec2& b, const Vec2& c)
{
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

void drawSplat(std::vector<Pixel>& pixels, uint32_t size, const Vec2& p, float radius, const Color3& color, float alpha)
{
    const int minX = static_cast<int>(std::floor(p.x - radius));
    const int maxX = static_cast<int>(std::ceil(p.x + radius));
    const int minY = static_cast<int>(std::floor(p.y - radius));
    const int maxY = static_cast<int>(std::ceil(p.y + radius));
    const float invRadius = 1.0f / std::max(radius, 1e-4f);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - p.x) * invRadius;
            const float dy = (static_cast<float>(y) + 0.5f - p.y) * invRadius;
            const float d2 = dx * dx + dy * dy;
            if (d2 > 1.0f)
                continue;
            blendPixel(pixels, size, x, y, color, alpha * (1.0f - d2) * (1.0f - d2));
        }
    }
}

void drawTriangle(
    std::vector<Pixel>& pixels, uint32_t size, const Vec2& p0, const Vec2& p1, const Vec2& p2, const Color3& color)
{
    const float area = edgeFunction(p0, p1, p2);
    if (std::abs(area) < 1e-5f) {
        drawSplat(pixels, size, p0, 1.1f, color, 0.22f);
        drawSplat(pixels, size, p1, 1.1f, color, 0.22f);
        drawSplat(pixels, size, p2, 1.1f, color, 0.22f);
        return;
    }

    const float minFx = std::min({p0.x, p1.x, p2.x}) - 1.0f;
    const float maxFx = std::max({p0.x, p1.x, p2.x}) + 1.0f;
    const float minFy = std::min({p0.y, p1.y, p2.y}) - 1.0f;
    const float maxFy = std::max({p0.y, p1.y, p2.y}) + 1.0f;
    const int minX = std::max(0, static_cast<int>(std::floor(minFx)));
    const int maxX = std::min(static_cast<int>(size) - 1, static_cast<int>(std::ceil(maxFx)));
    const int minY = std::max(0, static_cast<int>(std::floor(minFy)));
    const int maxY = std::min(static_cast<int>(size) - 1, static_cast<int>(std::ceil(maxFy)));
    const bool positive = area > 0.0f;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const float w0 = edgeFunction(p1, p2, p);
            const float w1 = edgeFunction(p2, p0, p);
            const float w2 = edgeFunction(p0, p1, p);
            const bool inside =
                positive ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (inside)
                blendPixel(pixels, size, x, y, color, 0.34f);
        }
    }

    drawSplat(pixels, size, p0, 1.0f, color, 0.13f);
    drawSplat(pixels, size, p1, 1.0f, color, 0.13f);
    drawSplat(pixels, size, p2, 1.0f, color, 0.13f);
}

std::optional<Alembic::AbcGeom::IPolyMesh> findFirstPolyMesh(const Alembic::Abc::IObject& obj)
{
    using namespace Alembic::Abc;
    using namespace Alembic::AbcGeom;

    if (IPolyMesh::matches(obj.getMetaData(), kSchemaTitleMatching))
        return IPolyMesh(obj, kWrapExisting, ErrorHandler::kThrowPolicy, kSchemaTitleMatching);

    const size_t childCount = obj.getNumChildren();
    for (size_t i = 0; i < childCount; ++i) {
        IObject child = obj.getChild(i);
        if (auto mesh = findFirstPolyMesh(child))
            return mesh;
    }
    return std::nullopt;
}

std::vector<FrameMesh> readAlembicMeshes(const fs::path& path, Box3& unionBox)
{
    using namespace Alembic::Abc;
    using namespace Alembic::AbcCoreFactory;
    using namespace Alembic::AbcGeom;

    IFactory factory;
    factory.setPolicy(ErrorHandler::kThrowPolicy);
    IFactory::CoreType coreType = IFactory::kUnknown;
    IArchive archive = factory.getArchive(path.string(), coreType);
    if (!archive.valid())
        throw std::runtime_error("failed to open Alembic archive " + path.string());

    std::optional<IPolyMesh> mesh = findFirstPolyMesh(archive.getTop());
    if (!mesh)
        throw std::runtime_error("no PolyMesh found in " + path.string());

    IPolyMeshSchema& schema = mesh->getSchema();
    const size_t sampleCount = schema.getNumSamples();
    if (sampleCount == 0)
        throw std::runtime_error("Alembic PolyMesh has no samples");

    IFloatArrayProperty colorBlendProp;
    ICompoundProperty arb = schema.getArbGeomParams();
    if (arb.valid() && arb.getPropertyHeader("color_blend"))
        colorBlendProp = IFloatArrayProperty(arb, "color_blend");

    std::vector<FrameMesh> frames;
    frames.reserve(sampleCount);
    for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        IPolyMeshSchema::Sample sample;
        schema.get(sample, ISampleSelector(static_cast<index_t>(sampleIndex)));
        if (!sample.valid())
            throw std::runtime_error("invalid PolyMesh sample " + std::to_string(sampleIndex));

        FrameMesh frame;
        const P3fArraySamplePtr positions = sample.getPositions();
        const Int32ArraySamplePtr indices = sample.getFaceIndices();
        const Int32ArraySamplePtr counts = sample.getFaceCounts();
        frame.positions.reserve(positions->size());
        for (size_t i = 0; i < positions->size(); ++i) {
            const auto& p = (*positions)[i];
            Vec3 out{p.x, p.y, p.z};
            frame.positions.push_back(out);
            expand(unionBox, out);
        }
        frame.indices.assign(indices->get(), indices->get() + indices->size());
        frame.counts.assign(counts->get(), counts->get() + counts->size());

        if (colorBlendProp.valid() && colorBlendProp.getNumSamples() > 0) {
            FloatArraySamplePtr colorBlend;
            colorBlendProp.get(colorBlend, ISampleSelector(static_cast<index_t>(sampleIndex)));
            if (colorBlend && colorBlend->size() == frame.positions.size())
                frame.colorBlend.assign(colorBlend->get(), colorBlend->get() + colorBlend->size());
        }

        frames.push_back(std::move(frame));
    }

    return frames;
}

Vec2 projectXY(const Vec3& p, const Box3& box, float padding, uint32_t tileSize)
{
    const Vec3 e = extent(box);
    const float minX = box.min.x - e.x * padding;
    const float maxX = box.max.x + e.x * padding;
    const float minY = box.min.y - e.y * padding;
    const float maxY = box.max.y + e.y * padding;
    const float u = (p.x - minX) / std::max(maxX - minX, 1e-4f);
    const float v = (p.y - minY) / std::max(maxY - minY, 1e-4f);
    return Vec2{u * static_cast<float>(tileSize - 1), (1.0f - v) * static_cast<float>(tileSize - 1)};
}

void bakeFlipbookFrame(
    std::vector<Pixel>& tile, const FrameMesh& frame, const Box3& box, float padding, uint32_t tileSize)
{
    const Color3 darkBlood{0.30f, 0.0f, 0.002f};
    const Color3 redBlood{0.78f, 0.005f, 0.0f};
    size_t indexOffset = 0;
    for (int32_t faceCount : frame.counts) {
        if (faceCount < 3 || indexOffset + static_cast<size_t>(faceCount) > frame.indices.size()) {
            indexOffset += static_cast<size_t>(std::max(faceCount, 0));
            continue;
        }

        const int32_t firstIndex = frame.indices[indexOffset];
        for (int32_t local = 1; local + 1 < faceCount; ++local) {
            const int32_t ia = firstIndex;
            const int32_t ib = frame.indices[indexOffset + static_cast<size_t>(local)];
            const int32_t ic = frame.indices[indexOffset + static_cast<size_t>(local + 1)];
            if (ia < 0 || ib < 0 || ic < 0 || static_cast<size_t>(ia) >= frame.positions.size() ||
                static_cast<size_t>(ib) >= frame.positions.size() || static_cast<size_t>(ic) >= frame.positions.size())
                continue;

            const float blendA =
                frame.colorBlend.empty() ? 0.55f : std::clamp(frame.colorBlend[static_cast<size_t>(ia)], 0.0f, 1.0f);
            const float blendB =
                frame.colorBlend.empty() ? 0.55f : std::clamp(frame.colorBlend[static_cast<size_t>(ib)], 0.0f, 1.0f);
            const float blendC =
                frame.colorBlend.empty() ? 0.55f : std::clamp(frame.colorBlend[static_cast<size_t>(ic)], 0.0f, 1.0f);
            const Color3 color = mixColor(darkBlood, redBlood, (blendA + blendB + blendC) / 3.0f);

            drawTriangle(tile,
                         tileSize,
                         projectXY(frame.positions[static_cast<size_t>(ia)], box, padding, tileSize),
                         projectXY(frame.positions[static_cast<size_t>(ib)], box, padding, tileSize),
                         projectXY(frame.positions[static_cast<size_t>(ic)], box, padding, tileSize),
                         color);
        }
        indexOffset += static_cast<size_t>(faceCount);
    }
}

void writeFlipbook(const Args& args, const std::vector<FrameMesh>& frames, const Box3& box)
{
    const uint32_t columns = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(frames.size()))));
    const uint32_t rows = static_cast<uint32_t>((frames.size() + columns - 1) / columns);
    const uint32_t atlasWidth = columns * args.flipbookSize;
    const uint32_t atlasHeight = rows * args.flipbookSize;
    std::vector<uint16_t> atlas(static_cast<size_t>(static_cast<uint64_t>(atlasWidth) * atlasHeight * 4u));
    std::vector<Pixel> tile(static_cast<size_t>(args.flipbookSize) * args.flipbookSize);

    for (size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        std::fill(tile.begin(), tile.end(), Pixel{});
        bakeFlipbookFrame(tile, frames[frameIndex], box, args.padding, args.flipbookSize);

        const uint32_t tileX = static_cast<uint32_t>(frameIndex % columns) * args.flipbookSize;
        const uint32_t tileY = static_cast<uint32_t>(frameIndex / columns) * args.flipbookSize;
        for (uint32_t y = 0; y < args.flipbookSize; ++y) {
            for (uint32_t x = 0; x < args.flipbookSize; ++x) {
                const Pixel& src = tile[static_cast<size_t>(y) * args.flipbookSize + x];
                const uint64_t dst = (static_cast<uint64_t>(tileY + y) * atlasWidth + tileX + x) * 4u;
                atlas[static_cast<size_t>(dst + 0)] = floatToHalf(src.r);
                atlas[static_cast<size_t>(dst + 1)] = floatToHalf(src.g);
                atlas[static_cast<size_t>(dst + 2)] = floatToHalf(src.b);
                atlas[static_cast<size_t>(dst + 3)] = floatToHalf(src.a);
            }
        }
    }

    const Vec3 e = extent(box);
    const float aspect = e.x / std::max(e.y, 1e-4f);
    const float worldHeight = 96.0f * (1.0f + args.padding * 2.0f);
    const float worldWidth = worldHeight * aspect;

    G2FlipHeader header{};
    std::memcpy(header.magic, "G2FLIP1", 7);
    header.version = 1;
    header.tileWidth = args.flipbookSize;
    header.tileHeight = args.flipbookSize;
    header.atlasWidth = atlasWidth;
    header.atlasHeight = atlasHeight;
    header.columns = columns;
    header.rows = rows;
    header.frameCount = static_cast<uint32_t>(frames.size());
    header.fps = args.fps;
    header.worldWidth = worldWidth;
    header.worldHeight = worldHeight;
    header.payloadFormat = k_flipPayloadRgba16FloatPremul;
    header.headerSize = sizeof(G2FlipHeader);
    header.payloadBytes = static_cast<uint64_t>(atlas.size()) * sizeof(uint16_t);

    fs::create_directories(args.flipbookPath.parent_path());
    std::ofstream out(args.flipbookPath, std::ios::binary);
    if (!out)
        throw std::runtime_error("failed to open flipbook output " + args.flipbookPath.string());
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(atlas.data()),
              static_cast<std::streamsize>(atlas.size() * sizeof(uint16_t)));
}

void splatVolume(std::vector<float>& volume,
                 uint32_t width,
                 uint32_t height,
                 uint32_t depth,
                 const Vec3& p,
                 const Box3& paddedBox,
                 float strength,
                 float radius)
{
    const Vec3 e = extent(paddedBox);
    const float fx = ((p.x - paddedBox.min.x) / e.x) * static_cast<float>(width - 1);
    const float fy = ((p.z - paddedBox.min.z) / e.z) * static_cast<float>(height - 1);
    const float fz = ((p.y - paddedBox.min.y) / e.y) * static_cast<float>(depth - 1);
    const int minX = static_cast<int>(std::floor(fx - radius));
    const int maxX = static_cast<int>(std::ceil(fx + radius));
    const int minY = static_cast<int>(std::floor(fy - radius));
    const int maxY = static_cast<int>(std::ceil(fy + radius));
    const int minZ = static_cast<int>(std::floor(fz - radius));
    const int maxZ = static_cast<int>(std::ceil(fz + radius));
    const float invRadius = 1.0f / std::max(radius, 1e-4f);

    for (int z = minZ; z <= maxZ; ++z) {
        if (z < 0 || z >= static_cast<int>(depth))
            continue;
        for (int y = minY; y <= maxY; ++y) {
            if (y < 0 || y >= static_cast<int>(height))
                continue;
            for (int x = minX; x <= maxX; ++x) {
                if (x < 0 || x >= static_cast<int>(width))
                    continue;
                const float dx = (static_cast<float>(x) - fx) * invRadius;
                const float dy = (static_cast<float>(y) - fy) * invRadius;
                const float dz = (static_cast<float>(z) - fz) * invRadius;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 1.0f)
                    continue;
                const float weight = (1.0f - d2) * (1.0f - d2);
                const uint64_t idx =
                    (static_cast<uint64_t>(z) * height + static_cast<uint32_t>(y)) * width + static_cast<uint32_t>(x);
                volume[static_cast<size_t>(idx)] = std::min(1.0f, volume[static_cast<size_t>(idx)] + strength * weight);
            }
        }
    }
}

Vec3 lerp(const Vec3& a, const Vec3& b, float t)
{
    return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

void bakeVolumeFrame(std::vector<float>& volume,
                     const FrameMesh& frame,
                     const Box3& paddedBox,
                     uint32_t width,
                     uint32_t height,
                     uint32_t depth)
{
    std::fill(volume.begin(), volume.end(), 0.0f);
    size_t indexOffset = 0;
    for (int32_t faceCount : frame.counts) {
        if (faceCount < 3 || indexOffset + static_cast<size_t>(faceCount) > frame.indices.size()) {
            indexOffset += static_cast<size_t>(std::max(faceCount, 0));
            continue;
        }
        const int32_t firstIndex = frame.indices[indexOffset];
        for (int32_t local = 1; local + 1 < faceCount; ++local) {
            const int32_t ia = firstIndex;
            const int32_t ib = frame.indices[indexOffset + static_cast<size_t>(local)];
            const int32_t ic = frame.indices[indexOffset + static_cast<size_t>(local + 1)];
            if (ia < 0 || ib < 0 || ic < 0 || static_cast<size_t>(ia) >= frame.positions.size() ||
                static_cast<size_t>(ib) >= frame.positions.size() || static_cast<size_t>(ic) >= frame.positions.size())
                continue;

            const Vec3 a = frame.positions[static_cast<size_t>(ia)];
            const Vec3 b = frame.positions[static_cast<size_t>(ib)];
            const Vec3 c = frame.positions[static_cast<size_t>(ic)];
            splatVolume(volume, width, height, depth, a, paddedBox, 0.14f, 1.45f);
            splatVolume(volume, width, height, depth, b, paddedBox, 0.14f, 1.45f);
            splatVolume(volume, width, height, depth, c, paddedBox, 0.14f, 1.45f);

            const float ab = std::hypot(std::hypot(a.x - b.x, a.y - b.y), a.z - b.z);
            const float ac = std::hypot(std::hypot(a.x - c.x, a.y - c.y), a.z - c.z);
            const float bc = std::hypot(std::hypot(b.x - c.x, b.y - c.y), b.z - c.z);
            const int subdivisions = std::clamp(static_cast<int>(std::ceil(std::max({ab, ac, bc}) * 56.0f)), 1, 5);
            for (int u = 0; u <= subdivisions; ++u) {
                for (int v = 0; v <= subdivisions - u; ++v) {
                    const float fu = static_cast<float>(u) / static_cast<float>(subdivisions);
                    const float fv = static_cast<float>(v) / static_cast<float>(subdivisions);
                    const float fw = 1.0f - fu - fv;
                    const Vec3 p{
                        a.x * fw + b.x * fu + c.x * fv, a.y * fw + b.y * fu + c.y * fv, a.z * fw + b.z * fu + c.z * fv};
                    splatVolume(volume, width, height, depth, p, paddedBox, 0.075f, 1.35f);
                }
            }
        }
        indexOffset += static_cast<size_t>(faceCount);
    }
}

void writeVolume(const Args& args, const std::vector<FrameMesh>& frames, const Box3& box)
{
    const Vec3 e = extent(box);
    const float maxExtent = std::max({e.x, e.y, e.z});
    const uint32_t width =
        std::max<uint32_t>(24, static_cast<uint32_t>(std::ceil((e.x / maxExtent) * args.volumeMaxDim)));
    const uint32_t height =
        std::max<uint32_t>(24, static_cast<uint32_t>(std::ceil((e.z / maxExtent) * args.volumeMaxDim)));
    const uint32_t depth =
        std::max<uint32_t>(24, static_cast<uint32_t>(std::ceil((e.y / maxExtent) * args.volumeMaxDim)));
    const uint64_t voxelsPerFrame = static_cast<uint64_t>(width) * height * depth;

    Box3 paddedBox = box;
    paddedBox.min.x -= e.x * args.padding;
    paddedBox.min.y -= e.y * args.padding;
    paddedBox.min.z -= e.z * args.padding;
    paddedBox.max.x += e.x * args.padding;
    paddedBox.max.y += e.y * args.padding;
    paddedBox.max.z += e.z * args.padding;

    G2VolHeader header{};
    std::memcpy(header.magic, "G2VOL1", 6);
    header.version = 1;
    header.width = width;
    header.height = height;
    header.depth = depth;
    header.frameCount = static_cast<uint32_t>(frames.size());
    header.fps = args.fps;
    header.bboxMin[0] = 0;
    header.bboxMin[1] = 0;
    header.bboxMin[2] = 0;
    header.bboxMax[0] = static_cast<int32_t>(width - 1);
    header.bboxMax[1] = static_cast<int32_t>(height - 1);
    header.bboxMax[2] = static_cast<int32_t>(depth - 1);
    header.voxelSize[0] = extent(paddedBox).x / static_cast<float>(width);
    header.voxelSize[1] = extent(paddedBox).z / static_cast<float>(height);
    header.voxelSize[2] = extent(paddedBox).y / static_cast<float>(depth);
    header.valueMax = 1.0f;
    header.valueP995 = 1.0f;
    header.payloadFormat = k_payloadR16FloatNormalized;
    header.headerSize = sizeof(G2VolHeader);
    header.payloadBytes = voxelsPerFrame * frames.size() * sizeof(uint16_t);

    fs::create_directories(args.volumePath.parent_path());
    std::ofstream out(args.volumePath, std::ios::binary);
    if (!out)
        throw std::runtime_error("failed to open volume output " + args.volumePath.string());
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    std::vector<float> volume(static_cast<size_t>(voxelsPerFrame));
    std::vector<uint16_t> halfVolume(static_cast<size_t>(voxelsPerFrame));
    for (const FrameMesh& frame : frames) {
        bakeVolumeFrame(volume, frame, paddedBox, width, height, depth);
        for (size_t i = 0; i < volume.size(); ++i)
            halfVolume[i] = floatToHalf(std::clamp(volume[i], 0.0f, 1.0f));
        out.write(reinterpret_cast<const char*>(halfVolume.data()),
                  static_cast<std::streamsize>(halfVolume.size() * sizeof(uint16_t)));
    }
}
} // namespace

int main(int argc, char** argv)
{
    try {
        const Args args = parseArgs(argc, argv);
        Box3 unionBox;
        std::vector<FrameMesh> frames = readAlembicMeshes(args.inputPath, unionBox);
        if (frames.empty())
            throw std::runtime_error("no frames read from Alembic archive");

        writeFlipbook(args, frames, unionBox);
        writeVolume(args, frames, unionBox);

        const Vec3 e = extent(unionBox);
        std::cout << args.inputPath.filename().string() << ": " << frames.size() << " frames, blood mesh bbox=" << e.x
                  << "x" << e.y << "x" << e.z << ", flipbook=" << args.flipbookPath << ", volume=" << args.volumePath
                  << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "abc_blood_to_g2fx: " << e.what() << "\n";
        return 1;
    }
}
