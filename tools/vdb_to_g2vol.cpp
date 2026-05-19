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
constexpr std::string_view k_gridName = "flames";
constexpr uint32_t k_formatR16FloatNormalized = 1u;

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

struct Args
{
    fs::path inputDir;
    fs::path outputPath;
    float fps = 24.0f;
    int maxDim = 192;
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
        throw std::runtime_error("usage: vdb_to_g2vol <input-dir> <output.g2vol> [--fps N] [--max-dim N]");
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
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + std::string(arg));
        }
    }

    if (args.fps <= 0.0f)
        throw std::runtime_error("--fps must be positive");
    if (args.maxDim <= 0)
        throw std::runtime_error("--max-dim must be positive");
    return args;
}

std::vector<fs::path> collectFrames(const fs::path& dir)
{
    static const std::regex k_nameRegex(R"(embergen_fire_a_([0-9]+)\.vdb)", std::regex::icase);

    std::vector<std::pair<int, fs::path>> numbered;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;

        std::smatch match;
        const std::string name = entry.path().filename().string();
        if (!std::regex_match(name, match, k_nameRegex))
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

openvdb::FloatGrid::Ptr readFlameGrid(const fs::path& path)
{
    openvdb::io::File file(path.string());
    file.open();
    openvdb::GridBase::Ptr base = file.readGrid(std::string(k_gridName));
    file.close();

    openvdb::FloatGrid::Ptr grid = openvdb::gridPtrCast<openvdb::FloatGrid>(base);
    if (!grid) {
        throw std::runtime_error(path.string() + " does not contain a FloatGrid named '" + std::string(k_gridName) +
                                 "'");
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

} // namespace

int main(int argc, char** argv)
{
    try {
        const Args args = parseArgs(argc, argv);
        const std::vector<fs::path> frames = collectFrames(args.inputDir);
        if (frames.empty()) {
            throw std::runtime_error("no embergen_fire_a_<n>.vdb frames found in " + args.inputDir.string());
        }

        openvdb::initialize();

        bool haveUnion = false;
        openvdb::CoordBBox unionBBox;
        openvdb::Vec3d voxelSize(1.0);
        size_t emptyFrameCount = 0;

        for (const fs::path& frame : frames) {
            openvdb::FloatGrid::Ptr grid = readFlameGrid(frame);
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
            throw std::runtime_error("all flame frames are empty");
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
            openvdb::FloatGrid::Ptr grid = readFlameGrid(frame);
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
            throw std::runtime_error("flame grid sampled to all zeros");

        fs::create_directories(args.outputPath.parent_path());
        std::ofstream out(args.outputPath, std::ios::binary);
        if (!out)
            throw std::runtime_error("failed to open output " + args.outputPath.string());

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

        out.write(reinterpret_cast<const char*>(&header), sizeof(header));

        std::vector<uint16_t> halfFrame(static_cast<size_t>(voxelsPerFrame));
        std::array<uint64_t, 4096> histogram{};
        uint64_t totalSamples = 0;

        for (const fs::path& frame : frames) {
            openvdb::FloatGrid::Ptr grid = readFlameGrid(frame);
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
                        halfFrame[dst++] = floatToHalf(normalized);
                    }
                }
            }
            out.write(reinterpret_cast<const char*>(halfFrame.data()),
                      static_cast<std::streamsize>(halfFrame.size() * sizeof(uint16_t)));
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

        out.seekp(0);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.close();

        std::cout << "Fire_01: " << frames.size() << " frames, grid '" << k_gridName << "', output " << outW << "x"
                  << outH << "x" << outD << ", maxValue=" << maxValue << ", p99.5Value=" << header.valueP995
                  << ", emptyFrames=" << emptyFrameCount << ", bytes=" << (sizeof(header) + header.payloadBytes)
                  << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "vdb_to_g2vol: " << e.what() << "\n";
        return 1;
    }
}
