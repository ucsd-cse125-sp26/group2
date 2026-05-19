/// @file VolumeFireEffect.cpp
/// @brief Runtime playback for generated fire/explosion flipbook previews.

#include "VolumeFireEffect.hpp"

#include "ecs/components/FireField.hpp"
#include "renderer-new/ShaderUtils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
#include <utility>

namespace
{
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
    float temperatureMax;
    float temperatureP995;
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

constexpr uint32_t k_cacheVersion = 1;
constexpr uint32_t k_payloadR16FloatNormalized = 1;
constexpr uint32_t k_flipPayloadRgba16FloatPremul = 1;

struct alignas(16) ParticleUniforms
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 camPos;
    float _p0;
    glm::vec3 camRight;
    float _p1;
    glm::vec3 camUp;
    float _p2;
};

struct alignas(16) VolumeFireParams
{
    glm::vec4 dimsAndBlend; // xyz = volume dimensions, w = frame blend
    glm::vec4 render;       // x = max steps, y = density, z = brightness, w = alpha scale
    glm::vec4 tint;         // rgb = display tint, a = reserved
};

struct alignas(16) FlipbookVertexUniforms
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 camRight;
    float halfWidth;
    glm::vec3 camUp;
    float halfHeight;
    glm::vec3 center;
    float opacity;
};

struct alignas(16) FlipbookFragmentUniforms
{
    glm::vec4 atlas;
    glm::vec4 anim;
};

SDL_GPUColorTargetBlendState premulAlphaBlend()
{
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    return b;
}
} // namespace

bool VolumeFireEffect::init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt)
{
    device_ = dev;
    shaderFmt_ = shaderFmt;

    const char* base = SDL_GetBasePath();
    const std::filesystem::path generatedPath = std::filesystem::path(base ? base : "") / "assets/generated";
    if (!createSampler() || !buildFlipbookPipeline(colorFmt, shaderFmt)) {
        SDL_Log("VolumeFireEffect: flipbook GPU setup failed; previews disabled");
    } else {
        loadFlipbookPreview(
            (generatedPath / "fire_01.g2flip").string(), glm::vec3{-300.0f, 50.0f, 500.0f}, 1.0f, 1.0f, "fire_01");
        loadFlipbookPreview((generatedPath / "grenade_dust_impact.g2flip").string(),
                            glm::vec3{-35.0f, 50.0f, 500.0f},
                            1.15f,
                            0.92f,
                            "grenade_dust_impact");
        loadFlipbookPreview((generatedPath / "midair_explosion_01.g2flip").string(),
                            glm::vec3{240.0f, 115.0f, 500.0f},
                            1.0f,
                            1.0f,
                            "midair_explosion_01");

        const uint32_t bloodPreviewIndex = static_cast<uint32_t>(flipbookPreviews_.size());
        if (loadFlipbookPreview((generatedPath / "blood_hit.g2flip").string(),
                                glm::vec3{495.0f, 70.0f, 500.0f},
                                1.35f,
                                1.0f,
                                "blood_hit"))
        {
            bloodFlipbookIndex_ = bloodPreviewIndex;
        }
    }

    if (loadCache((generatedPath / "blood_hit.g2vol").string())) {
        const float maxWorldDim =
            std::max({static_cast<float>(width_), static_cast<float>(height_), static_cast<float>(depth_)});
        const float scale = 140.0f / std::max(maxWorldDim, 1.0f);
        volumeWorldSize_ = glm::vec3{static_cast<float>(width_) * scale,
                                     static_cast<float>(depth_) * scale,
                                     static_cast<float>(height_) * scale};
        volumeBottomCenter_ = glm::vec3{650.0f, 70.0f, 500.0f};
        if (createGpuResources() && buildPipeline(colorFmt, shaderFmt)) {
            volumeReady_ = true;
            SDL_Log("VolumeFireEffect: loaded blood_hit 3D volume preview (%ux%ux%u, %u frames)",
                    width_,
                    height_,
                    depth_,
                    frameCount_);
        } else {
            frames_.clear();
            volumeReady_ = false;
        }
    }

    ready_ = !flipbookPreviews_.empty() || volumeReady_;
    if (!volumeReady_ && !flipbookPreviews_.empty()) {
        frameCount_ = flipbookPreviews_.front().frameCount;
        fps_ = flipbookPreviews_.front().fps;
    }
    SDL_Log("VolumeFireEffect: loaded %zu flipbook VFX previews", flipbookPreviews_.size());
    return true;
}

void VolumeFireEffect::quit()
{
    if (device_) {
        SDL_WaitForGPUIdle(device_);
        if (pipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        if (flipbookPipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, flipbookPipeline_);
        for (SDL_GPUTexture*& texture : frameTextures_) {
            if (texture)
                SDL_ReleaseGPUTexture(device_, texture);
            texture = nullptr;
        }
        if (flipbookTexture_)
            SDL_ReleaseGPUTexture(device_, flipbookTexture_);
        for (FlipbookPreview& preview : flipbookPreviews_) {
            if (preview.texture)
                SDL_ReleaseGPUTexture(device_, preview.texture);
            preview.texture = nullptr;
        }
        if (sampler_)
            SDL_ReleaseGPUSampler(device_, sampler_);
        if (frameTransfer_)
            SDL_ReleaseGPUTransferBuffer(device_, frameTransfer_);
    }

    pipeline_ = nullptr;
    flipbookPipeline_ = nullptr;
    flipbookTexture_ = nullptr;
    sampler_ = nullptr;
    frameTransfer_ = nullptr;
    instanceBuf_.quit();
    frames_.clear();
    flipbookPixels_.clear();
    instances_.clear();
    flipbookPreviews_.clear();
    transientFlipbooks_.clear();
    bloodFlipbookIndex_ = UINT32_MAX;
    volumeReady_ = false;
    ready_ = false;
    flipbookReady_ = false;
    device_ = nullptr;
}

bool VolumeFireEffect::loadCache(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    G2VolHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in)
        return false;

    if (std::memcmp(header.magic, "G2VOL1", 6) != 0 || header.version != k_cacheVersion ||
        header.payloadFormat != k_payloadR16FloatNormalized || header.headerSize < sizeof(G2VolHeader))
    {
        SDL_Log("VolumeFireEffect: invalid cache header in %s", path.c_str());
        return false;
    }
    if (header.width == 0 || header.height == 0 || header.depth == 0 || header.frameCount == 0) {
        SDL_Log("VolumeFireEffect: invalid zero dimensions in %s", path.c_str());
        return false;
    }

    width_ = header.width;
    height_ = header.height;
    depth_ = header.depth;
    frameCount_ = header.frameCount;
    fps_ = header.fps > 0.0f ? header.fps : 24.0f;
    frameBytes_ = static_cast<uint64_t>(width_) * height_ * depth_ * sizeof(uint16_t);
    const uint64_t expectedPayload = frameBytes_ * frameCount_;
    if (expectedPayload != header.payloadBytes) {
        SDL_Log("VolumeFireEffect: payload size mismatch in %s", path.c_str());
        return false;
    }

    frames_.resize(static_cast<size_t>(expectedPayload / sizeof(uint16_t)));
    in.seekg(static_cast<std::streamoff>(header.headerSize), std::ios::beg);
    in.read(reinterpret_cast<char*>(frames_.data()), static_cast<std::streamsize>(expectedPayload));
    if (!in) {
        SDL_Log("VolumeFireEffect: failed reading payload in %s", path.c_str());
        frames_.clear();
        return false;
    }
    return true;
}

bool VolumeFireEffect::loadFlipbook(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    G2FlipHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in)
        return false;

    if (std::memcmp(header.magic, "G2FLIP1", 7) != 0 || header.version != k_cacheVersion ||
        header.payloadFormat != k_flipPayloadRgba16FloatPremul || header.headerSize < sizeof(G2FlipHeader))
    {
        SDL_Log("VolumeFireEffect: invalid flipbook header in %s", path.c_str());
        return false;
    }
    if (header.tileWidth == 0 || header.tileHeight == 0 || header.atlasWidth == 0 || header.atlasHeight == 0 ||
        header.columns == 0 || header.rows == 0 || header.frameCount == 0)
    {
        SDL_Log("VolumeFireEffect: invalid flipbook dimensions in %s", path.c_str());
        return false;
    }

    const uint64_t expectedPayload =
        static_cast<uint64_t>(header.atlasWidth) * header.atlasHeight * 4u * sizeof(uint16_t);
    if (expectedPayload != header.payloadBytes) {
        SDL_Log("VolumeFireEffect: flipbook payload size mismatch in %s", path.c_str());
        return false;
    }

    flipbookTileWidth_ = header.tileWidth;
    flipbookTileHeight_ = header.tileHeight;
    flipbookAtlasWidth_ = header.atlasWidth;
    flipbookAtlasHeight_ = header.atlasHeight;
    flipbookColumns_ = header.columns;
    flipbookRows_ = header.rows;
    flipbookFrameCount_ = header.frameCount;
    flipbookPayloadBytes_ = header.payloadBytes;

    flipbookPixels_.resize(static_cast<size_t>(expectedPayload / sizeof(uint16_t)));
    in.seekg(static_cast<std::streamoff>(header.headerSize), std::ios::beg);
    in.read(reinterpret_cast<char*>(flipbookPixels_.data()), static_cast<std::streamsize>(expectedPayload));
    if (!in) {
        SDL_Log("VolumeFireEffect: failed reading flipbook payload in %s", path.c_str());
        flipbookPixels_.clear();
        return false;
    }
    return true;
}

bool VolumeFireEffect::createSampler()
{
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(device_, &sci);
    if (!sampler_) {
        SDL_Log("VolumeFireEffect: SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool VolumeFireEffect::loadFlipbookPreview(
    const std::string& path, const glm::vec3& bottomCenter, float worldScale, float opacity, const std::string& name)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        SDL_Log("VolumeFireEffect: flipbook preview unavailable at %s", path.c_str());
        return false;
    }

    G2FlipHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in)
        return false;

    if (std::memcmp(header.magic, "G2FLIP1", 7) != 0 || header.version != k_cacheVersion ||
        header.payloadFormat != k_flipPayloadRgba16FloatPremul || header.headerSize < sizeof(G2FlipHeader))
    {
        SDL_Log("VolumeFireEffect: invalid flipbook header in %s", path.c_str());
        return false;
    }
    if (header.tileWidth == 0 || header.tileHeight == 0 || header.atlasWidth == 0 || header.atlasHeight == 0 ||
        header.columns == 0 || header.rows == 0 || header.frameCount == 0)
    {
        SDL_Log("VolumeFireEffect: invalid flipbook dimensions in %s", path.c_str());
        return false;
    }

    const uint64_t expectedPayload =
        static_cast<uint64_t>(header.atlasWidth) * header.atlasHeight * 4u * sizeof(uint16_t);
    if (expectedPayload != header.payloadBytes) {
        SDL_Log("VolumeFireEffect: flipbook payload size mismatch in %s", path.c_str());
        return false;
    }

    std::vector<uint16_t> pixels(static_cast<size_t>(expectedPayload / sizeof(uint16_t)));
    in.seekg(static_cast<std::streamoff>(header.headerSize), std::ios::beg);
    in.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(expectedPayload));
    if (!in) {
        SDL_Log("VolumeFireEffect: failed reading flipbook payload in %s", path.c_str());
        return false;
    }

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = header.atlasWidth;
    tci.height = header.atlasHeight;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &tci);
    if (!texture) {
        SDL_Log("VolumeFireEffect: SDL_CreateGPUTexture(%s) failed: %s", name.c_str(), SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = static_cast<Uint32>(expectedPayload);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbci);
    if (!transfer) {
        SDL_Log("VolumeFireEffect: SDL_CreateGPUTransferBuffer(%s) failed: %s", name.c_str(), SDL_GetError());
        SDL_ReleaseGPUTexture(device_, texture);
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (!mapped) {
        SDL_Log("VolumeFireEffect: SDL_MapGPUTransferBuffer(%s) failed: %s", name.c_str(), SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTexture(device_, texture);
        return false;
    }
    std::memcpy(mapped, pixels.data(), static_cast<size_t>(expectedPayload));
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("VolumeFireEffect: SDL_AcquireGPUCommandBuffer(%s) failed: %s", name.c_str(), SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTexture(device_, texture);
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    if (!copyPass) {
        SDL_Log("VolumeFireEffect: SDL_BeginGPUCopyPass(%s) failed: %s", name.c_str(), SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTexture(device_, texture);
        return false;
    }
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    src.pixels_per_row = header.atlasWidth;
    src.rows_per_layer = header.atlasHeight;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture;
    dst.w = header.atlasWidth;
    dst.h = header.atlasHeight;
    dst.d = 1;
    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);

    const float worldWidth = header.worldWidth > 0.0f ? header.worldWidth : static_cast<float>(header.tileWidth);
    const float worldHeight = header.worldHeight > 0.0f ? header.worldHeight : static_cast<float>(header.tileHeight);
    FlipbookPreview preview{};
    preview.texture = texture;
    preview.tileWidth = header.tileWidth;
    preview.tileHeight = header.tileHeight;
    preview.atlasWidth = header.atlasWidth;
    preview.atlasHeight = header.atlasHeight;
    preview.columns = header.columns;
    preview.rows = header.rows;
    preview.frameCount = header.frameCount;
    preview.fps = header.fps > 0.0f ? header.fps : 24.0f;
    preview.halfWidth = worldWidth * worldScale * 0.5f;
    preview.halfHeight = worldHeight * worldScale * 0.5f;
    preview.opacity = opacity;
    preview.bottomCenter = bottomCenter;
    preview.name = name;
    flipbookPreviews_.push_back(std::move(preview));

    SDL_Log("VolumeFireEffect: loaded %s flipbook (%ux%u atlas, %u frames)",
            name.c_str(),
            header.atlasWidth,
            header.atlasHeight,
            header.frameCount);
    return true;
}

bool VolumeFireEffect::createGpuResources()
{
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_3D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = width_;
    tci.height = height_;
    tci.layer_count_or_depth = depth_;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    for (SDL_GPUTexture*& texture : frameTextures_) {
        texture = SDL_CreateGPUTexture(device_, &tci);
        if (!texture) {
            SDL_Log("VolumeFireEffect: SDL_CreateGPUTexture(3D) failed: %s", SDL_GetError());
            return false;
        }
    }

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = static_cast<Uint32>(frameBytes_);
    frameTransfer_ = SDL_CreateGPUTransferBuffer(device_, &tbci);
    if (!frameTransfer_) {
        SDL_Log("VolumeFireEffect: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }

    if (!sampler_ && !createSampler())
        return false;

    instanceBuf_.init(device_, sizeof(VolumeFireInstanceGPU) * 8u);

    if (!flipbookPixels_.empty() && !createFlipbookTexture()) {
        SDL_Log("VolumeFireEffect: flipbook texture setup failed; flipbook preview disabled");
        flipbookPixels_.clear();
    }

    return true;
}

bool VolumeFireEffect::createFlipbookTexture()
{
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = flipbookAtlasWidth_;
    tci.height = flipbookAtlasHeight_;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    flipbookTexture_ = SDL_CreateGPUTexture(device_, &tci);
    if (!flipbookTexture_) {
        SDL_Log("VolumeFireEffect: SDL_CreateGPUTexture(flipbook) failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = static_cast<Uint32>(flipbookPayloadBytes_);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbci);
    if (!transfer) {
        SDL_Log("VolumeFireEffect: SDL_CreateGPUTransferBuffer(flipbook) failed: %s", SDL_GetError());
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (!mapped) {
        SDL_Log("VolumeFireEffect: SDL_MapGPUTransferBuffer(flipbook) failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return false;
    }
    std::memcpy(mapped, flipbookPixels_.data(), static_cast<size_t>(flipbookPayloadBytes_));
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("VolumeFireEffect: SDL_AcquireGPUCommandBuffer(flipbook) failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    if (!copyPass) {
        SDL_Log("VolumeFireEffect: SDL_BeginGPUCopyPass(flipbook) failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return false;
    }
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    src.pixels_per_row = flipbookAtlasWidth_;
    src.rows_per_layer = flipbookAtlasHeight_;

    SDL_GPUTextureRegion dst{};
    dst.texture = flipbookTexture_;
    dst.w = flipbookAtlasWidth_;
    dst.h = flipbookAtlasHeight_;
    dst.d = 1;
    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    flipbookPixels_.clear();
    flipbookReady_ = true;
    return true;
}

bool VolumeFireEffect::buildPipeline(SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt)
{
    const char* base = SDL_GetBasePath();
    const char* ext = (shaderFmt == SDL_GPU_SHADERFORMAT_MSL)    ? ".msl"
                      : (shaderFmt == SDL_GPU_SHADERFORMAT_DXIL) ? ".dxil"
                                                                 : ".spv";
    char vp[512], fp[512];
    SDL_snprintf(vp, sizeof(vp), "%sshaders/volume_fire.vert%s", base ? base : "", ext);
    SDL_snprintf(fp, sizeof(fp), "%sshaders/volume_fire.frag%s", base ? base : "", ext);

    SDL_GPUShader* vert = loadShader(device_, vp, shaderFmt, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1, 0);
    SDL_GPUShader* frag = loadShader(device_, fp, shaderFmt, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device_, vert);
        SDL_ReleaseGPUShader(device_, frag);
        return false;
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = colorFmt;
    ctd.blend_state = premulAlphaBlend();

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = false;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pci);
    SDL_ReleaseGPUShader(device_, vert);
    SDL_ReleaseGPUShader(device_, frag);
    if (!pipeline_) {
        SDL_Log("VolumeFireEffect: pipeline creation failed: %s", SDL_GetError());
        return false;
    }
    if (flipbookReady_ && !buildFlipbookPipeline(colorFmt, shaderFmt)) {
        SDL_Log("VolumeFireEffect: flipbook pipeline creation failed; flipbook preview disabled");
        flipbookReady_ = false;
    }
    return true;
}

bool VolumeFireEffect::buildFlipbookPipeline(SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt)
{
    const char* base = SDL_GetBasePath();
    const char* ext = (shaderFmt == SDL_GPU_SHADERFORMAT_MSL)    ? ".msl"
                      : (shaderFmt == SDL_GPU_SHADERFORMAT_DXIL) ? ".dxil"
                                                                 : ".spv";
    char vp[512], fp[512];
    SDL_snprintf(vp, sizeof(vp), "%sshaders/fire_flipbook.vert%s", base ? base : "", ext);
    SDL_snprintf(fp, sizeof(fp), "%sshaders/fire_flipbook.frag%s", base ? base : "", ext);

    SDL_GPUShader* vert = loadShader(device_, vp, shaderFmt, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader* frag = loadShader(device_, fp, shaderFmt, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device_, vert);
        SDL_ReleaseGPUShader(device_, frag);
        return false;
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = colorFmt;
    ctd.blend_state = premulAlphaBlend();

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = false;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    flipbookPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pci);
    SDL_ReleaseGPUShader(device_, vert);
    SDL_ReleaseGPUShader(device_, frag);
    if (!flipbookPipeline_) {
        SDL_Log("VolumeFireEffect: flipbook pipeline creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void VolumeFireEffect::update(float dt, Registry& registry)
{
    (void)registry;
    instances_.clear();
    if (!ready_)
        return;

    animTime_ += std::max(0.0f, dt);
    for (TransientFlipbook& instance : transientFlipbooks_)
        instance.age += std::max(0.0f, dt);
    transientFlipbooks_.erase(std::remove_if(transientFlipbooks_.begin(),
                                             transientFlipbooks_.end(),
                                             [](const TransientFlipbook& instance) {
                                                 return instance.lifetime > 0.0f && instance.age >= instance.lifetime;
                                             }),
                              transientFlipbooks_.end());

    if (volumeReady_ && frameCount_ > 0) {
        const float frameFloat = std::fmod(animTime_ * fps_, static_cast<float>(frameCount_));
        currentFrame_ = static_cast<uint32_t>(std::floor(frameFloat)) % frameCount_;
        nextFrame_ = (currentFrame_ + 1) % frameCount_;
        frameBlend_ = frameFloat - std::floor(frameFloat);

        const glm::vec3 half{volumeWorldSize_.x * 0.5f, 0.0f, volumeWorldSize_.z * 0.5f};
        VolumeFireInstanceGPU instance{};
        instance.boxMin =
            glm::vec3{volumeBottomCenter_.x - half.x, volumeBottomCenter_.y, volumeBottomCenter_.z - half.z};
        instance.boxMax = glm::vec3{
            volumeBottomCenter_.x + half.x, volumeBottomCenter_.y + volumeWorldSize_.y, volumeBottomCenter_.z + half.z};
        instance.opacity = volumeOpacity_;
        instances_.push_back(instance);
    } else if (!flipbookPreviews_.empty()) {
        const FlipbookPreview& preview = flipbookPreviews_.front();
        const float frameFloat = std::fmod(animTime_ * preview.fps, static_cast<float>(preview.frameCount));
        currentFrame_ = static_cast<uint32_t>(std::floor(frameFloat)) % preview.frameCount;
        nextFrame_ = (currentFrame_ + 1) % preview.frameCount;
        frameBlend_ = frameFloat - std::floor(frameFloat);
    }
}

void VolumeFireEffect::uploadFrameToTexture(SDL_GPUCommandBuffer* cmd, uint32_t slot, uint32_t frameIndex)
{
    if (slot >= 2 || frameIndex >= frameCount_ || textureFrame_[slot] == frameIndex)
        return;

    const uint64_t byteOffset = static_cast<uint64_t>(frameIndex) * frameBytes_;
    void* mapped = SDL_MapGPUTransferBuffer(device_, frameTransfer_, true);
    if (!mapped) {
        SDL_Log("VolumeFireEffect: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        return;
    }
    std::memcpy(
        mapped, reinterpret_cast<const uint8_t*>(frames_.data()) + byteOffset, static_cast<size_t>(frameBytes_));
    SDL_UnmapGPUTransferBuffer(device_, frameTransfer_);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = frameTransfer_;
    src.offset = 0;
    src.pixels_per_row = width_;
    src.rows_per_layer = height_;

    SDL_GPUTextureRegion dst{};
    dst.texture = frameTextures_[slot];
    dst.w = width_;
    dst.h = height_;
    dst.d = depth_;
    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    textureFrame_[slot] = frameIndex;
}

uint32_t VolumeFireEffect::findTextureSlot(uint32_t frameIndex) const
{
    if (textureFrame_[0] == frameIndex)
        return 0;
    if (textureFrame_[1] == frameIndex)
        return 1;
    return UINT32_MAX;
}

void VolumeFireEffect::uploadToGpu(SDL_GPUCommandBuffer* cmd)
{
    if (!volumeReady_ || !cmd)
        return;

    if (findTextureSlot(currentFrame_) == UINT32_MAX)
        uploadFrameToTexture(cmd, 0, currentFrame_);
    if (findTextureSlot(nextFrame_) == UINT32_MAX)
        uploadFrameToTexture(cmd, 1, nextFrame_);

    instanceBuf_.upload(cmd,
                        instances_.empty() ? nullptr : instances_.data(),
                        static_cast<uint32_t>(instances_.size()),
                        sizeof(VolumeFireInstanceGPU));
}

void VolumeFireEffect::spawnBloodHit(glm::vec3 pos, glm::vec3 normal)
{
    if (bloodFlipbookIndex_ == UINT32_MAX || bloodFlipbookIndex_ >= flipbookPreviews_.size())
        return;

    const FlipbookPreview& preview = flipbookPreviews_[bloodFlipbookIndex_];
    const float targetHeight = 72.0f;
    const float scale = targetHeight / std::max(preview.halfHeight * 2.0f, 1.0f);
    const glm::vec3 safeNormal = glm::length(normal) > 1e-4f ? glm::normalize(normal) : glm::vec3{0.0f, 1.0f, 0.0f};

    TransientFlipbook instance{};
    instance.previewIndex = bloodFlipbookIndex_;
    instance.center = pos + safeNormal * 6.0f + glm::vec3{0.0f, targetHeight * 0.10f, 0.0f};
    instance.halfWidth = preview.halfWidth * scale;
    instance.halfHeight = preview.halfHeight * scale;
    instance.opacity = 1.0f;
    instance.age = 0.0f;
    instance.lifetime = static_cast<float>(preview.frameCount) / std::max(preview.fps, 1.0f);
    transientFlipbooks_.push_back(instance);
}

void VolumeFireEffect::renderFlipbook(SDL_GPURenderPass* pass,
                                      SDL_GPUCommandBuffer* cmd,
                                      const NewCamera& camera,
                                      const FlipbookPreview& preview,
                                      const glm::vec3& center,
                                      float halfWidth,
                                      float halfHeight,
                                      float opacity,
                                      float age,
                                      bool loop) const
{
    if (!preview.texture || preview.frameCount == 0)
        return;

    float frameFloat = age * preview.fps;
    if (loop) {
        frameFloat = std::fmod(frameFloat, static_cast<float>(preview.frameCount));
    } else {
        frameFloat = std::min(frameFloat, static_cast<float>(preview.frameCount - 1));
    }
    const uint32_t flipCurrent = static_cast<uint32_t>(std::floor(frameFloat)) % preview.frameCount;
    const uint32_t flipNext =
        loop ? (flipCurrent + 1) % preview.frameCount : std::min(flipCurrent + 1, preview.frameCount - 1);
    const float flipBlend = frameFloat - std::floor(frameFloat);

    FlipbookVertexUniforms vu{};
    vu.view = camera.getViewMatrix();
    vu.proj = camera.getProjectionMatrix();
    vu.camRight = camera.getRight();
    vu.halfWidth = halfWidth;
    vu.camUp = camera.getUp();
    vu.halfHeight = halfHeight;
    vu.center = center;
    vu.opacity = opacity;
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));

    FlipbookFragmentUniforms fu{};
    fu.atlas = glm::vec4{static_cast<float>(preview.columns),
                         static_cast<float>(preview.rows),
                         static_cast<float>(flipCurrent),
                         static_cast<float>(flipNext)};
    fu.anim = glm::vec4{flipBlend, 0.0f, 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));

    SDL_GPUTextureSamplerBinding flipSampler{preview.texture, sampler_};
    SDL_BindGPUFragmentSamplers(pass, 0, &flipSampler, 1);
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
}

void VolumeFireEffect::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const NewCamera& camera)
{
    if (!ready_ || !sampler_)
        return;

    if (volumeReady_ && pipeline_ && instanceBuf_.liveCount() > 0) {
        const uint32_t currentSlot = findTextureSlot(currentFrame_);
        const uint32_t nextSlot = findTextureSlot(nextFrame_);
        if (currentSlot != UINT32_MAX && nextSlot != UINT32_MAX) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline_);
            instanceBuf_.bindAsVertexStorage(pass, 0);

            ParticleUniforms pu{};
            pu.view = camera.getViewMatrix();
            pu.proj = camera.getProjectionMatrix();
            pu.camPos = camera.getEye();
            pu.camRight = camera.getRight();
            pu.camUp = camera.getUp();
            SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));

            VolumeFireParams params{};
            params.dimsAndBlend = glm::vec4{
                static_cast<float>(width_), static_cast<float>(height_), static_cast<float>(depth_), frameBlend_};
            params.render = glm::vec4{112.0f, 3.2f, 1.15f, 1.0f};
            params.tint = glm::vec4{0.70f, 0.0f, 0.006f, 1.0f};
            SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));

            SDL_GPUTextureSamplerBinding volumeSamplers[2]{{frameTextures_[currentSlot], sampler_},
                                                           {frameTextures_[nextSlot], sampler_}};
            SDL_BindGPUFragmentSamplers(pass, 0, volumeSamplers, 2);
            SDL_DrawGPUPrimitives(pass, 36, instanceBuf_.liveCount(), 0, 0);
        }
    }

    if (!flipbookPipeline_ || flipbookPreviews_.empty())
        return;

    SDL_BindGPUGraphicsPipeline(pass, flipbookPipeline_);
    for (const FlipbookPreview& preview : flipbookPreviews_) {
        renderFlipbook(pass,
                       cmd,
                       camera,
                       preview,
                       preview.bottomCenter + glm::vec3{0.0f, preview.halfHeight, 0.0f},
                       preview.halfWidth,
                       preview.halfHeight,
                       preview.opacity,
                       animTime_,
                       true);
    }

    for (const TransientFlipbook& instance : transientFlipbooks_) {
        if (instance.previewIndex >= flipbookPreviews_.size())
            continue;
        const float fade =
            1.0f - std::clamp((instance.age - instance.lifetime * 0.72f) / std::max(instance.lifetime * 0.28f, 1e-4f),
                              0.0f,
                              1.0f);
        renderFlipbook(pass,
                       cmd,
                       camera,
                       flipbookPreviews_[instance.previewIndex],
                       instance.center,
                       instance.halfWidth,
                       instance.halfHeight,
                       instance.opacity * fade,
                       instance.age,
                       false);
    }
}
