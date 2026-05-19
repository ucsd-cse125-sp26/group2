/// @file VolumeFireEffect.cpp
/// @brief Runtime playback and raymarched rendering for generated animated fire volumes.

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

constexpr uint32_t k_cacheVersion = 1;
constexpr uint32_t k_payloadR16FloatNormalized = 1;
constexpr uint32_t k_maxInstances = 128;

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
    instanceBuf_.init(dev, sizeof(VolumeFireInstanceGPU) * k_maxInstances);

    const char* base = SDL_GetBasePath();
    const std::filesystem::path cachePath = std::filesystem::path(base ? base : "") / "assets/generated/fire_01.g2vol";
    if (!loadCache(cachePath.string())) {
        SDL_Log("VolumeFireEffect: fire volume cache unavailable at %s", cachePath.string().c_str());
        return true;
    }

    if (!createGpuResources() || !buildPipeline(colorFmt, shaderFmt)) {
        SDL_Log("VolumeFireEffect: GPU setup failed; volume fire disabled");
        return true;
    }

    ready_ = true;
    SDL_Log("VolumeFireEffect: loaded fire_01.g2vol (%ux%ux%u, %u frames @ %.1f fps)",
            width_,
            height_,
            depth_,
            frameCount_,
            static_cast<double>(fps_));
    return true;
}

void VolumeFireEffect::quit()
{
    if (device_) {
        SDL_WaitForGPUIdle(device_);
        if (pipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        for (SDL_GPUTexture*& texture : frameTextures_) {
            if (texture)
                SDL_ReleaseGPUTexture(device_, texture);
            texture = nullptr;
        }
        if (sampler_)
            SDL_ReleaseGPUSampler(device_, sampler_);
        if (frameTransfer_)
            SDL_ReleaseGPUTransferBuffer(device_, frameTransfer_);
    }

    pipeline_ = nullptr;
    sampler_ = nullptr;
    frameTransfer_ = nullptr;
    instanceBuf_.quit();
    frames_.clear();
    instances_.clear();
    ready_ = false;
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
    return true;
}

void VolumeFireEffect::update(float dt, Registry& registry)
{
    instances_.clear();
    if (!ready_)
        return;

    animTime_ += std::max(0.0f, dt);
    const float frameFloat = std::fmod(animTime_ * fps_, static_cast<float>(frameCount_));
    currentFrame_ = static_cast<uint32_t>(std::floor(frameFloat)) % frameCount_;
    nextFrame_ = (currentFrame_ + 1) % frameCount_;
    frameBlend_ = frameFloat - std::floor(frameFloat);

    auto makeInstance = [&](glm::vec3 anchor, glm::vec3 scale, float opacity) {
        const glm::vec3 worldSize =
            glm::vec3{static_cast<float>(width_), static_cast<float>(height_), static_cast<float>(depth_)} * scale;
        return VolumeFireInstanceGPU{
            .boxMin = anchor + glm::vec3{-worldSize.x * 0.5f, 0.0f, -worldSize.z * 0.5f},
            .opacity = opacity,
            .boxMax = anchor + glm::vec3{worldSize.x * 0.5f, worldSize.y, worldSize.z * 0.5f},
            ._pad0 = 0.0f,
        };
    };

    instances_.push_back(makeInstance(glm::vec3{-300.0f, 50.0f, 500.0f}, glm::vec3{1.0f}, 1.0f));

    registry.view<FireField>().each([&](const FireField& field) {
        if (instances_.size() >= k_maxInstances || field.radius <= 0.0f || field.remaining <= 0.0f)
            return;

        const float horizontalDim = static_cast<float>(std::max(width_, depth_));
        const float scale = horizontalDim > 0.0f ? (field.radius * 2.0f) / horizontalDim : 1.0f;
        const float fade = std::clamp(field.remaining / 0.5f, 0.0f, 1.0f);
        instances_.push_back(makeInstance(field.position, glm::vec3{scale}, fade));
    });
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

void VolumeFireEffect::uploadToGpu(SDL_GPUCommandBuffer* cmd)
{
    instanceBuf_.upload(
        cmd, instances_.data(), static_cast<uint32_t>(instances_.size()), sizeof(VolumeFireInstanceGPU));
    if (!ready_ || instances_.empty())
        return;

    uploadFrameToTexture(cmd, 0, currentFrame_);
    uploadFrameToTexture(cmd, 1, nextFrame_);
}

void VolumeFireEffect::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const NewCamera& camera)
{
    if (!ready_ || !pipeline_ || instanceBuf_.liveCount() == 0 || !frameTextures_[0] || !frameTextures_[1] || !sampler_)
        return;

    ParticleUniforms pu{};
    pu.view = camera.getViewMatrix();
    pu.proj = camera.getProjectionMatrix();
    pu.camPos = camera.getEye();
    pu.camRight = camera.getRight();
    pu.camUp = camera.getUp();
    SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));

    VolumeFireParams fp{};
    fp.dimsAndBlend =
        glm::vec4{static_cast<float>(width_), static_cast<float>(height_), static_cast<float>(depth_), frameBlend_};
    fp.render = glm::vec4{96.0f, 4.0f, 5.5f, 1.0f};
    SDL_PushGPUFragmentUniformData(cmd, 0, &fp, sizeof(fp));

    SDL_BindGPUGraphicsPipeline(pass, pipeline_);
    instanceBuf_.bindAsVertexStorage(pass, 0);
    SDL_GPUTextureSamplerBinding samplers[2] = {
        SDL_GPUTextureSamplerBinding{frameTextures_[0], sampler_},
        SDL_GPUTextureSamplerBinding{frameTextures_[1], sampler_},
    };
    SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);
    SDL_DrawGPUPrimitives(pass, 36, instanceBuf_.liveCount(), 0, 0);
}
