/// @file vdb_to_g2vol.cpp
/// @brief Convert an EmberGen/OpenVDB flame sequence into a compact runtime volume cache.

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <openvdb/openvdb.h>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr uint32_t k_formatR16FloatNormalized = 1u;
constexpr uint32_t k_flipPayloadRgba16FloatPremul = 1u;

#pragma pack(push, 1)
struct G2VolHeader
{
    char magic[8]; // "G2VOL1\0"
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
    char magic[8]; // "G2FLIP1\0"
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

struct Args
{
    fs::path inputDir;
    fs::path outputPath;
    fs::path flipbookPath;
    std::string gridName = "flames";
    std::string colorMode = "fire";
    float fps = 24.0f;
    int maxDim = 192;
    int flipbookSize = 256;
    bool flipbookOnly = false;
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

    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }

    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | ((mantissa + 0x1000u) >> 13));
}

Args parseArgs(int argc, char** argv)
{
    if (argc < 3) {
        throw std::runtime_error("usage: vdb_to_g2vol <input-dir> <output.g2vol> [--fps N] [--max-dim N] "
                                 "[--grid NAME] [--color fire|dust] [--flipbook-output output.g2flip] "
                                 "[--flipbook-size N] [--flipbook-only]");
    }

    Args args;
    args.inputDir = argv[1];
    args.outputPath = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--fps" && i + 1 < argc) {
            args.fps = std::stof(argv[++i]);
        } else if (arg == "--max-dim" && i + 1 < argc) {
            args.maxDim = std::stoi(argv[++i]);
        } else if (arg == "--flipbook-output" && i + 1 < argc) {
            args.flipbookPath = argv[++i];
        } else if (arg == "--flipbook-size" && i + 1 < argc) {
            args.flipbookSize = std::stoi(argv[++i]);
        } else if (arg == "--grid" && i + 1 < argc) {
            args.gridName = argv[++i];
        } else if (arg == "--color" && i + 1 < argc) {
            args.colorMode = argv[++i];
        } else if (arg == "--flipbook-only") {
            args.flipbookOnly = true;
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + std::string(arg));
        }
    }

    if (args.fps <= 0.0f)
        throw std::runtime_error("--fps must be positive");
    if (args.maxDim <= 0)
        throw std::runtime_error("--max-dim must be positive");
    if (args.flipbookSize <= 0)
        throw std::runtime_error("--flipbook-size must be positive");
    if (args.gridName.empty())
        throw std::runtime_error("--grid must not be empty");
    if (args.colorMode != "fire" && args.colorMode != "dust")
        throw std::runtime_error("--color must be either 'fire' or 'dust'");
    if (args.flipbookOnly && args.flipbookPath.empty())
        throw std::runtime_error("--flipbook-only requires --flipbook-output");
    return args;
}

std::vector<fs::path> collectFrames(const fs::path& dir)
{
    static const std::regex k_numberedVdbRegex(R"(.*?([0-9]+)\.vdb)", std::regex::icase);

    std::vector<std::pair<int, fs::path>> numbered;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;

        std::smatch match;
        const std::string name = entry.path().filename().string();
        if (!std::regex_match(name, match, k_numberedVdbRegex))
            continue;

        numbered.emplace_back(std::stoi(match[1].str()), entry.path());
    }

    std::sort(numbered.begin(), numbered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<fs::path> frames;
    frames.reserve(numbered.size());
    for (const auto& [_, path] : numbered)
        frames.push_back(path);
    return frames;
}

openvdb::FloatGrid::Ptr readFloatGrid(const fs::path& path, const std::string& gridName)
{
    openvdb::io::File file(path.string());
    file.open();
    openvdb::GridBase::Ptr base = file.readGrid(gridName);
    file.close();

    openvdb::FloatGrid::Ptr grid = openvdb::gridPtrCast<openvdb::FloatGrid>(base);
    if (!grid) {
        throw std::runtime_error(path.string() + " does not contain a FloatGrid named '" + gridName + "'");
    }
    return grid;
}

float sampleTrilinear(const openvdb::FloatGrid::ConstAccessor& acc, double x, double y, double z)
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const double tx = x - static_cast<double>(x0);
    const double ty = y - static_cast<double>(y0);
    const double tz = z - static_cast<double>(z0);

    auto value = [&](int xi, int yi, int zi) { return static_cast<double>(acc.getValue(openvdb::Coord(xi, yi, zi))); };

    const double c000 = value(x0, y0, z0);
    const double c100 = value(x0 + 1, y0, z0);
    const double c010 = value(x0, y0 + 1, z0);
    const double c110 = value(x0 + 1, y0 + 1, z0);
    const double c001 = value(x0, y0, z0 + 1);
    const double c101 = value(x0 + 1, y0, z0 + 1);
    const double c011 = value(x0, y0 + 1, z0 + 1);
    const double c111 = value(x0 + 1, y0 + 1, z0 + 1);

    const double c00 = c000 + (c100 - c000) * tx;
    const double c10 = c010 + (c110 - c010) * tx;
    const double c01 = c001 + (c101 - c001) * tx;
    const double c11 = c011 + (c111 - c011) * tx;
    const double c0 = c00 + (c10 - c00) * ty;
    const double c1 = c01 + (c11 - c01) * ty;
    return static_cast<float>(std::max(0.0, c0 + (c1 - c0) * tz));
}

float sampleDenseTrilinear(
    const std::vector<float>& values, uint32_t width, uint32_t height, uint32_t depth, float x, float y, float z)
{
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    z = std::clamp(z, 0.0f, static_cast<float>(depth - 1));

    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t z0 = static_cast<uint32_t>(std::floor(z));
    const uint32_t x1 = std::min(width - 1, x0 + 1);
    const uint32_t y1 = std::min(height - 1, y0 + 1);
    const uint32_t z1 = std::min(depth - 1, z0 + 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float tz = z - static_cast<float>(z0);

    auto at = [&](uint32_t xi, uint32_t yi, uint32_t zi) {
        const uint64_t idx = (static_cast<uint64_t>(zi) * height + yi) * width + xi;
        return values[static_cast<size_t>(idx)];
    };

    const float c000 = at(x0, y0, z0);
    const float c100 = at(x1, y0, z0);
    const float c010 = at(x0, y1, z0);
    const float c110 = at(x1, y1, z0);
    const float c001 = at(x0, y0, z1);
    const float c101 = at(x1, y0, z1);
    const float c011 = at(x0, y1, z1);
    const float c111 = at(x1, y1, z1);

    const float c00 = c000 + (c100 - c000) * tx;
    const float c10 = c010 + (c110 - c010) * tx;
    const float c01 = c001 + (c101 - c001) * tx;
    const float c11 = c011 + (c111 - c011) * tx;
    const float c0 = c00 + (c10 - c00) * ty;
    const float c1 = c01 + (c11 - c01) * ty;
    return c0 + (c1 - c0) * tz;
}

float smoothstep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::array<float, 3> fireColor(float t)
{
    const std::array<float, 3> ember{0.82f, 0.055f, 0.01f};
    const std::array<float, 3> orange{1.0f, 0.29f, 0.035f};
    const std::array<float, 3> gold{1.0f, 0.61f, 0.13f};
    const std::array<float, 3> hot{1.0f, 0.78f, 0.34f};
    auto mix3 = [](const std::array<float, 3>& a, const std::array<float, 3>& b, float f) {
        return std::array<float, 3>{a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
    };

    std::array<float, 3> c = mix3(ember, orange, smoothstep(0.04f, 0.38f, t));
    c = mix3(c, gold, smoothstep(0.34f, 0.78f, t));
    c = mix3(c, hot, smoothstep(0.86f, 1.0f, t));
    return c;
}

std::array<float, 3> dustColor(float t)
{
    const std::array<float, 3> dark{0.21f, 0.18f, 0.15f};
    const std::array<float, 3> mid{0.48f, 0.40f, 0.30f};
    const std::array<float, 3> lit{0.77f, 0.68f, 0.50f};
    auto mix3 = [](const std::array<float, 3>& a, const std::array<float, 3>& b, float f) {
        return std::array<float, 3>{a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
    };

    std::array<float, 3> c = mix3(dark, mid, smoothstep(0.05f, 0.55f, t));
    c = mix3(c, lit, smoothstep(0.62f, 1.0f, t));
    return c;
}

void compositeFlipbookFrame(std::vector<uint16_t>& atlas,
                            uint32_t atlasWidth,
                            uint32_t tileSize,
                            uint32_t tileX,
                            uint32_t tileY,
                            const std::vector<float>& frameValues,
                            uint32_t width,
                            uint32_t height,
                            uint32_t depth,
                            const std::string& colorMode)
{
    const uint32_t depthSamples = std::min<uint32_t>(128, std::max<uint32_t>(32, height));
    for (uint32_t py = 0; py < tileSize; ++py) {
        const float z =
            (1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(tileSize)) * static_cast<float>(depth - 1);
        for (uint32_t px = 0; px < tileSize; ++px) {
            const float x =
                ((static_cast<float>(px) + 0.5f) / static_cast<float>(tileSize)) * static_cast<float>(width - 1);
            float accumR = 0.0f;
            float accumG = 0.0f;
            float accumB = 0.0f;
            float accumA = 0.0f;

            for (uint32_t s = 0; s < depthSamples && accumA < 0.985f; ++s) {
                const float y = ((static_cast<float>(s) + 0.5f) / static_cast<float>(depthSamples)) *
                                static_cast<float>(height - 1);
                float value = sampleDenseTrilinear(frameValues, width, height, depth, x, y, z);
                float alpha = 0.0f;
                float brightness = 1.0f;
                std::array<float, 3> color{};
                if (colorMode == "dust") {
                    value = std::pow(smoothstep(0.025f, 0.82f, value), 1.02f);
                    alpha = std::clamp(std::pow(value, 1.12f) * 0.035f, 0.0f, 0.16f);
                    brightness = 1.15f;
                    color = dustColor(value);
                } else {
                    value = std::pow(smoothstep(0.009f, 0.78f, value), 1.18f);
                    alpha = std::clamp(std::pow(value, 1.55f) * 0.050f, 0.0f, 0.22f);
                    brightness = 3.85f;
                    color = fireColor(value);
                }
                accumR += (1.0f - accumA) * color[0] * brightness * alpha;
                accumG += (1.0f - accumA) * color[1] * brightness * alpha;
                accumB += (1.0f - accumA) * color[2] * brightness * alpha;
                accumA += (1.0f - accumA) * alpha;
            }

            const uint64_t dstPixel = (static_cast<uint64_t>(tileY + py) * atlasWidth + (tileX + px)) * 4u;
            atlas[static_cast<size_t>(dstPixel + 0)] = floatToHalf(accumR);
            atlas[static_cast<size_t>(dstPixel + 1)] = floatToHalf(accumG);
            atlas[static_cast<size_t>(dstPixel + 2)] = floatToHalf(accumB);
            atlas[static_cast<size_t>(dstPixel + 3)] = floatToHalf(accumA);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Args args = parseArgs(argc, argv);
        const std::vector<fs::path> frames = collectFrames(args.inputDir);
        if (frames.empty()) {
            throw std::runtime_error("no numerically named .vdb frames found in " + args.inputDir.string());
        }

        openvdb::initialize();

        bool haveUnion = false;
        openvdb::CoordBBox unionBBox;
        openvdb::Vec3d voxelSize(1.0);
        size_t emptyFrameCount = 0;

        for (const fs::path& frame : frames) {
            openvdb::FloatGrid::Ptr grid = readFloatGrid(frame, args.gridName);
            openvdb::CoordBBox bbox;
            if (!grid->tree().evalActiveVoxelBoundingBox(bbox)) {
                ++emptyFrameCount;
                continue;
            }
            if (!haveUnion) {
                unionBBox = bbox;
                voxelSize = grid->transform().voxelSize();
                haveUnion = true;
            } else {
                unionBBox.expand(bbox);
            }
        }
        if (!haveUnion) {
            throw std::runtime_error("all '" + args.gridName + "' frames are empty");
        }

        const openvdb::Coord sourceDim = unionBBox.dim();
        const int nativeMaxDim = std::max({sourceDim.x(), sourceDim.y(), sourceDim.z()});
        const double resizeScale = nativeMaxDim > args.maxDim ? static_cast<double>(args.maxDim) / nativeMaxDim : 1.0;
        const uint32_t outW =
            static_cast<uint32_t>(std::max(1, static_cast<int>(std::ceil(sourceDim.x() * resizeScale))));
        const uint32_t outH =
            static_cast<uint32_t>(std::max(1, static_cast<int>(std::ceil(sourceDim.y() * resizeScale))));
        const uint32_t outD =
            static_cast<uint32_t>(std::max(1, static_cast<int>(std::ceil(sourceDim.z() * resizeScale))));
        const uint64_t voxelsPerFrame = static_cast<uint64_t>(outW) * outH * outD;

        float maxValue = 0.0f;
        for (const fs::path& frame : frames) {
            openvdb::FloatGrid::Ptr grid = readFloatGrid(frame, args.gridName);
            const auto acc = grid->getConstAccessor();
            for (uint32_t z = 0; z < outD; ++z) {
                const double sz = unionBBox.min().z() + ((z + 0.5) / outD) * sourceDim.z() - 0.5;
                for (uint32_t y = 0; y < outH; ++y) {
                    const double sy = unionBBox.min().y() + ((y + 0.5) / outH) * sourceDim.y() - 0.5;
                    for (uint32_t x = 0; x < outW; ++x) {
                        const double sx = unionBBox.min().x() + ((x + 0.5) / outW) * sourceDim.x() - 0.5;
                        maxValue = std::max(maxValue, sampleTrilinear(acc, sx, sy, sz));
                    }
                }
            }
        }
        if (maxValue <= 0.0f)
            throw std::runtime_error("'" + args.gridName + "' grid sampled to all zeros");

        const bool writeVolume = !args.flipbookOnly;
        std::ofstream out;
        G2VolHeader header{};
        std::memcpy(header.magic, "G2VOL1", 6);
        header.version = 1;
        header.width = outW;
        header.height = outH;
        header.depth = outD;
        header.frameCount = static_cast<uint32_t>(frames.size());
        header.fps = args.fps;
        header.bboxMin[0] = unionBBox.min().x();
        header.bboxMin[1] = unionBBox.min().y();
        header.bboxMin[2] = unionBBox.min().z();
        header.bboxMax[0] = unionBBox.max().x();
        header.bboxMax[1] = unionBBox.max().y();
        header.bboxMax[2] = unionBBox.max().z();
        header.voxelSize[0] = static_cast<float>(voxelSize.x());
        header.voxelSize[1] = static_cast<float>(voxelSize.y());
        header.voxelSize[2] = static_cast<float>(voxelSize.z());
        header.valueMax = maxValue;
        header.payloadFormat = k_formatR16FloatNormalized;
        header.headerSize = sizeof(G2VolHeader);
        header.payloadBytes = voxelsPerFrame * frames.size() * sizeof(uint16_t);

        if (writeVolume) {
            fs::create_directories(args.outputPath.parent_path());
            out.open(args.outputPath, std::ios::binary);
            if (!out)
                throw std::runtime_error("failed to open output " + args.outputPath.string());
            out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        }

        std::vector<uint16_t> halfFrame(writeVolume ? static_cast<size_t>(voxelsPerFrame) : 0);
        std::vector<float> normalizedFrame(args.flipbookPath.empty() ? 0 : static_cast<size_t>(voxelsPerFrame));
        std::array<uint64_t, 4096> histogram{};
        uint64_t totalSamples = 0;

        const bool writeFlipbook = !args.flipbookPath.empty();
        const uint32_t flipTileSize = static_cast<uint32_t>(args.flipbookSize);
        const uint32_t flipColumns = writeFlipbook ? static_cast<uint32_t>(std::ceil(std::sqrt(frames.size()))) : 0;
        const uint32_t flipRows =
            writeFlipbook ? static_cast<uint32_t>((frames.size() + flipColumns - 1) / flipColumns) : 0;
        const uint32_t flipAtlasWidth = flipColumns * flipTileSize;
        const uint32_t flipAtlasHeight = flipRows * flipTileSize;
        std::vector<uint16_t> flipAtlas;
        if (writeFlipbook) {
            flipAtlas.resize(static_cast<size_t>(static_cast<uint64_t>(flipAtlasWidth) * flipAtlasHeight * 4u));
        }

        for (size_t frameOrdinal = 0; frameOrdinal < frames.size(); ++frameOrdinal) {
            const fs::path& frame = frames[frameOrdinal];
            openvdb::FloatGrid::Ptr grid = readFloatGrid(frame, args.gridName);
            const auto acc = grid->getConstAccessor();
            size_t dst = 0;
            for (uint32_t z = 0; z < outD; ++z) {
                const double sz = unionBBox.min().z() + ((z + 0.5) / outD) * sourceDim.z() - 0.5;
                for (uint32_t y = 0; y < outH; ++y) {
                    const double sy = unionBBox.min().y() + ((y + 0.5) / outH) * sourceDim.y() - 0.5;
                    for (uint32_t x = 0; x < outW; ++x) {
                        const double sx = unionBBox.min().x() + ((x + 0.5) / outW) * sourceDim.x() - 0.5;
                        const float normalized = std::clamp(sampleTrilinear(acc, sx, sy, sz) / maxValue, 0.0f, 1.0f);
                        const size_t bin = std::min<size_t>(histogram.size() - 1,
                                                            static_cast<size_t>(normalized * (histogram.size() - 1)));
                        ++histogram[bin];
                        ++totalSamples;
                        if (writeVolume)
                            halfFrame[dst] = floatToHalf(normalized);
                        if (writeFlipbook)
                            normalizedFrame[dst] = normalized;
                        ++dst;
                    }
                }
            }
            if (writeVolume) {
                out.write(reinterpret_cast<const char*>(halfFrame.data()),
                          static_cast<std::streamsize>(halfFrame.size() * sizeof(uint16_t)));
            }

            if (writeFlipbook) {
                const uint32_t col = static_cast<uint32_t>(frameOrdinal % flipColumns);
                const uint32_t row = static_cast<uint32_t>(frameOrdinal / flipColumns);
                compositeFlipbookFrame(flipAtlas,
                                       flipAtlasWidth,
                                       flipTileSize,
                                       col * flipTileSize,
                                       row * flipTileSize,
                                       normalizedFrame,
                                       outW,
                                       outH,
                                       outD,
                                       args.colorMode);
            }
        }

        uint64_t cumulative = 0;
        const uint64_t percentileTarget = static_cast<uint64_t>(std::ceil(totalSamples * 0.995));
        size_t percentileBin = 0;
        for (; percentileBin < histogram.size(); ++percentileBin) {
            cumulative += histogram[percentileBin];
            if (cumulative >= percentileTarget)
                break;
        }
        header.valueP995 = (static_cast<float>(percentileBin) / static_cast<float>(histogram.size() - 1)) * maxValue;

        if (writeVolume) {
            out.seekp(0);
            out.write(reinterpret_cast<const char*>(&header), sizeof(header));
            out.close();
        }

        if (writeFlipbook) {
            fs::create_directories(args.flipbookPath.parent_path());
            std::ofstream flipOut(args.flipbookPath, std::ios::binary);
            if (!flipOut)
                throw std::runtime_error("failed to open flipbook output " + args.flipbookPath.string());

            G2FlipHeader flipHeader{};
            std::memcpy(flipHeader.magic, "G2FLIP1", 7);
            flipHeader.version = 1;
            flipHeader.tileWidth = flipTileSize;
            flipHeader.tileHeight = flipTileSize;
            flipHeader.atlasWidth = flipAtlasWidth;
            flipHeader.atlasHeight = flipAtlasHeight;
            flipHeader.columns = flipColumns;
            flipHeader.rows = flipRows;
            flipHeader.frameCount = static_cast<uint32_t>(frames.size());
            flipHeader.fps = args.fps;
            flipHeader.worldWidth = static_cast<float>(outW);
            flipHeader.worldHeight = static_cast<float>(outD);
            flipHeader.payloadFormat = k_flipPayloadRgba16FloatPremul;
            flipHeader.headerSize = sizeof(G2FlipHeader);
            flipHeader.payloadBytes = static_cast<uint64_t>(flipAtlas.size()) * sizeof(uint16_t);
            flipOut.write(reinterpret_cast<const char*>(&flipHeader), sizeof(flipHeader));
            flipOut.write(reinterpret_cast<const char*>(flipAtlas.data()),
                          static_cast<std::streamsize>(flipAtlas.size() * sizeof(uint16_t)));
            flipOut.close();
        }

        std::cout << args.inputDir.filename().string() << ": " << frames.size() << " frames, grid '" << args.gridName
                  << "', output " << outW << "x" << outH << "x" << outD << ", maxValue=" << maxValue
                  << ", p99.5Value=" << header.valueP995 << ", emptyFrames=" << emptyFrameCount
                  << ", bytes=" << (writeVolume ? sizeof(header) + header.payloadBytes : 0)
                  << (writeFlipbook ? ", flipbookBytes=" : "")
                  << (writeFlipbook ? std::to_string(sizeof(G2FlipHeader) + flipAtlas.size() * sizeof(uint16_t)) : "")
                  << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "vdb_to_g2vol: " << e.what() << "\n";
        return 1;
    }
}
