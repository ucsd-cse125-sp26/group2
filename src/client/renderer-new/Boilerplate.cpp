/// @file Boilerplate.cpp
/// @brief Implementation of SDL3 GPU boilerplate helpers.

#include "Boilerplate.hpp"

#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Boilerplate
{

namespace
{
constexpr Uint32 kMaxCubeArrayLayersForMetal = 336;
}

ImGui_ImplSDLGPU3_InitInfo createImGuiInfo(SDL_GPUDevice* device, SDL_Window* window)
{
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = device;
    info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    return info;
}

SDL_GPUVertexAttribute
makeAttribute(Uint32 location, SDL_GPUVertexElementFormat format, Uint32 offset, Uint32 bufferSlot)
{
    SDL_GPUVertexAttribute attribute{};
    attribute.location = location;
    attribute.buffer_slot = bufferSlot;
    attribute.format = format;
    attribute.offset = offset;
    return attribute;
}

SDL_GPUColorTargetInfo makeColorTargetClear(SDL_GPUTexture* texture, SDL_FColor clearColor)
{
    SDL_GPUColorTargetInfo target{};
    target.texture = texture;
    target.clear_color = clearColor;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    return target;
}

SDL_GPUColorTargetInfo makeColorTargetLoad(SDL_GPUTexture* texture)
{
    SDL_GPUColorTargetInfo target{};
    target.texture = texture;
    target.load_op = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    return target;
}

SDL_GPUDepthStencilTargetInfo makeDepthTarget(SDL_GPUTexture* texture, Uint8 layer, bool store)
{
    SDL_GPUDepthStencilTargetInfo target{};
    target.texture = texture;
    target.clear_depth = 1.0f;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    target.cycle = false;
    target.layer = layer;
    target.clear_stencil = 0;
    return target;
}

SDL_GPUTextureSamplerBinding makeTextureSamplerBinding(SDL_GPUTexture* texture, SDL_GPUSampler* sampler)
{
    SDL_GPUTextureSamplerBinding binding{};
    binding.texture = texture;
    binding.sampler = sampler;
    return binding;
}

SDL_GPUShaderFormat selectShaderFormat(SDL_GPUDevice* device)
{
    const SDL_GPUShaderFormat availableFormats = SDL_GetGPUShaderFormats(device);

#ifdef HAVE_DXIL_SHADERS
    if (availableFormats & SDL_GPU_SHADERFORMAT_DXIL)
        return SDL_GPU_SHADERFORMAT_DXIL;
#endif

#ifdef HAVE_MSL_SHADERS
    if (availableFormats & SDL_GPU_SHADERFORMAT_MSL)
        return SDL_GPU_SHADERFORMAT_MSL;
#endif

    if (availableFormats & SDL_GPU_SHADERFORMAT_SPIRV)
        return SDL_GPU_SHADERFORMAT_SPIRV;

    return SDL_GPU_SHADERFORMAT_INVALID;
}

SDL_GPUShader* loadShader(SDL_GPUDevice* device,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount)
{
    const bool isMsl = format == SDL_GPU_SHADERFORMAT_MSL;
    const bool isDxil = format == SDL_GPU_SHADERFORMAT_DXIL;
    const char* const base = SDL_GetBasePath();

    std::filesystem::path fullPath = base ? base : "";
    fullPath /= path;
    fullPath += isMsl ? ".msl" : isDxil ? ".dxil" : ".spv";

    const std::string fullPathString = fullPath.string();

    size_t codeSize = 0;
    void* code = SDL_LoadFile(fullPathString.c_str(), &codeSize);
    if (!code) {
        SDL_Log("loadShader: failed to load shader (%s): %s", fullPathString.c_str(), SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code_size = static_cast<Uint32>(codeSize);
    info.code = static_cast<const Uint8*>(code);
    info.entrypoint = isMsl ? "main0" : "main";
    info.format = format;
    info.stage = stage;
    info.num_samplers = samplerCount;
    info.num_uniform_buffers = uniformBufferCount;
    info.num_storage_buffers = storageBufferCount;
    info.num_storage_textures = storageTextureCount;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(code);

    if (!shader)
        SDL_Log("loadShader: SDL_CreateGPUShader(%s) failed: %s", fullPathString.c_str(), SDL_GetError());

    return shader;
}

SDL_GPUShader* loadShader(SDL_GPUDevice* device, const ShaderInfo& shaderInfo, SDL_GPUShaderFormat format)
{
    return loadShader(device,
                      shaderInfo.path,
                      format,
                      shaderInfo.stage,
                      shaderInfo.samplerCount,
                      shaderInfo.uniformBufferCount,
                      shaderInfo.storageBufferCount,
                      shaderInfo.storageTextureCount);
}

SDL_GPUGraphicsPipeline* createGraphicsDepthPipeline(SDL_GPUDevice* device, PipelineDescription pipelineDesc)
{
    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* fragmentShader = nullptr;

    if (pipelineDesc.vertexShaderInfo && pipelineDesc.fragmentShaderInfo) {
        vertexShader = loadShader(device, *pipelineDesc.vertexShaderInfo, pipelineDesc.shaderFormat);
        fragmentShader = loadShader(device, *pipelineDesc.fragmentShaderInfo, pipelineDesc.shaderFormat);
    } else {
        return nullptr;
    }

    if (!vertexShader || !fragmentShader) {
        return nullptr;
    }

    if (!pipelineDesc.vertexInputLayout) {
        return nullptr;
    }

    SDL_GPUVertexInputState vertexInputState{};
    vertexInputState.num_vertex_buffers =
        static_cast<Uint32>(pipelineDesc.vertexInputLayout->bufferDescriptions.size());
    vertexInputState.vertex_buffer_descriptions = pipelineDesc.vertexInputLayout->bufferDescriptions.data();
    vertexInputState.num_vertex_attributes = static_cast<Uint32>(pipelineDesc.vertexInputLayout->attributes.size());
    vertexInputState.vertex_attributes = pipelineDesc.vertexInputLayout->attributes.data();

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state = vertexInputState;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.color_target_descriptions = pipelineDesc.colorTarget;
    pipelineInfo.target_info.num_color_targets = pipelineDesc.colorTarget ? 1 : 0;
    pipelineInfo.target_info.has_depth_stencil_target = pipelineDesc.depthTest || pipelineDesc.depthWrite;
    pipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test = pipelineDesc.depthTest;
    pipelineInfo.depth_stencil_state.enable_depth_write = pipelineDesc.depthWrite;

    pipelineInfo.rasterizer_state = pipelineDesc.rasterizer_state;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    return pipeline;
}
SDL_GPUGraphicsPipeline* createGraphicsPipeline(SDL_GPUDevice* device,
                                                SDL_GPUTextureFormat& colorFormat,
                                                SDL_GPUShaderFormat shaderFormat,
                                                const ShaderInfo& vertexShaderInfo,
                                                const ShaderInfo& fragmentShaderInfo,
                                                const VertexInputLayout& vertexInputLayout,
                                                bool enableDepth,
                                                bool overBlending)
{
    SDL_GPUShader* vertexShader = loadShader(device, vertexShaderInfo, shaderFormat);
    SDL_GPUShader* fragmentShader = loadShader(device, fragmentShaderInfo, shaderFormat);

    if (!vertexShader || !fragmentShader) {
        SDL_ReleaseGPUShader(device, vertexShader);
        SDL_ReleaseGPUShader(device, fragmentShader);
        return nullptr;
    }

    SDL_GPUVertexInputState vertexInputState{};
    vertexInputState.num_vertex_buffers = static_cast<Uint32>(vertexInputLayout.bufferDescriptions.size());
    vertexInputState.vertex_buffer_descriptions = vertexInputLayout.bufferDescriptions.data();
    vertexInputState.num_vertex_attributes = static_cast<Uint32>(vertexInputLayout.attributes.size());
    vertexInputState.vertex_attributes = vertexInputLayout.attributes.data();

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = colorFormat;
    if (overBlending) {
        SDL_GPUColorTargetBlendState overBlendState{};
        overBlendState.enable_blend = true;

        overBlendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        overBlendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        overBlendState.color_blend_op = SDL_GPU_BLENDOP_ADD;

        overBlendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        overBlendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        overBlendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        colorTarget.blend_state = overBlendState;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state = vertexInputState;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.has_depth_stencil_target = enableDepth;
    pipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test = enableDepth;
    pipelineInfo.depth_stencil_state.enable_depth_write = enableDepth;

    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    return pipeline;
}

SDL_GPUBuffer* createBuffer(SDL_GPUDevice* device, size_t bufferSize, SDL_GPUBufferUsageFlags usage)
{
    SDL_GPUBufferCreateInfo info{};
    info.size = static_cast<Uint32>(bufferSize);
    info.usage = usage;

    return SDL_CreateGPUBuffer(device, &info);
}

SDL_GPUTransferBuffer* createTransferBuffer(SDL_GPUDevice* device, size_t transferBufferSize, bool upload)
{
    SDL_GPUTransferBufferCreateInfo info{};
    info.size = static_cast<Uint32>(transferBufferSize);
    info.usage = upload ? SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD : SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;

    return SDL_CreateGPUTransferBuffer(device, &info);
}

SDL_GPUTransferBuffer* createUploadBuffer(SDL_GPUDevice* device, size_t transferBufferSize)
{
    return createTransferBuffer(device, transferBufferSize, true);
}

void uploadBuffers(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd, const std::vector<BufferUpload>& uploads)
{
    size_t totalSize = 0;
    for (const BufferUpload& upload : uploads)
        totalSize += upload.size;

    if (totalSize == 0)
        return;

    SDL_GPUTransferBuffer* transferBuffer = createUploadBuffer(device, totalSize);
    if (!transferBuffer) {
        SDL_Log("uploadBuffers: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return;
    }

    auto* transferData = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transferBuffer, false));
    if (!transferData) {
        SDL_Log("uploadBuffers: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        return;
    }

    size_t offset = 0;
    for (const BufferUpload& upload : uploads) {
        SDL_memcpy(transferData + offset, upload.data, upload.size);
        offset += upload.size;
    }

    SDL_UnmapGPUTransferBuffer(device, transferBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    offset = 0;
    for (const BufferUpload& upload : uploads) {
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        source.offset = static_cast<Uint32>(offset);

        SDL_GPUBufferRegion dest{};
        dest.buffer = upload.buffer;
        dest.offset = 0;
        dest.size = static_cast<Uint32>(upload.size);

        SDL_UploadToGPUBuffer(copyPass, &source, &dest, true);
        offset += upload.size;
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
}

SDL_GPUTexture* createEmptyTextureD32F(SDL_GPUDevice* device, Uint32 width, Uint32 height, bool cube, Uint32 arraySize)
{
    SDL_GPUTextureCreateInfo textureInfo{};
    if (cube) {
        const Uint32 maxCubeEntries = kMaxCubeArrayLayersForMetal / 6u;
        if (arraySize > maxCubeEntries) {
            SDL_Log("createEmptyTextureD32F: clamping cube-array entries from %u to %u to stay within Metal "
                    "texture array limits",
                    static_cast<unsigned>(arraySize),
                    static_cast<unsigned>(maxCubeEntries));
            arraySize = maxCubeEntries;
        }
    }

    const bool array = arraySize > 1;

    if (cube) {
        if (array) {
            textureInfo.type = SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
        } else {
            textureInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        }
    } else {
        if (array) {
            textureInfo.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        } else {
            textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
        }
    }

    textureInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = (cube ? 6 : 1) * arraySize;
    textureInfo.num_levels = 1;
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
    if (!texture) {
        SDL_Log("createEmptyTextureD32F: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return nullptr;
    }

    return texture;
}

SDL_GPUTexture*
createTextureRGBA8(SDL_GPUDevice* device, Uint32 width, Uint32 height, const void* data, SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = format;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
    if (!texture) {
        SDL_Log("createTextureRGBA8: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return nullptr;
    }

    const size_t uploadSize = static_cast<size_t>(width) * height * 4;
    SDL_GPUTransferBuffer* transferBuffer = createUploadBuffer(device, uploadSize);
    if (!transferBuffer) {
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
    if (!mapped) {
        SDL_Log("createTextureRGBA8: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }

    SDL_memcpy(mapped, data, uploadSize);
    SDL_UnmapGPUTransferBuffer(device, transferBuffer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transferBuffer;
    source.offset = 0;
    source.pixels_per_row = width;
    source.rows_per_layer = height;

    SDL_GPUTextureRegion dest{};
    dest.texture = texture;
    dest.w = width;
    dest.h = height;
    dest.d = 1;

    SDL_UploadToGPUTexture(copyPass, &source, &dest, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);

    return texture;
}

SDL_GPUTexture* loadTexture(SDL_GPUDevice* device, const char* path)
{
    std::filesystem::path fullPath = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    fullPath /= path;

    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        SDL_Log("loadTexture: failed to load %s: %s", fullPath.string().c_str(), stbi_failure_reason());
        return nullptr;
    }

    SDL_GPUTexture* texture = createTextureRGBA8(device,
                                                 static_cast<Uint32>(width),
                                                 static_cast<Uint32>(height),
                                                 pixels,
                                                 SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
    stbi_image_free(pixels);

    return texture;
}

SDL_GPUTexture* createDepthTexture(SDL_GPUDevice* device, Uint32 width, Uint32 height)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
    if (!texture)
        SDL_Log("createDepthTexture: SDL_CreateGPUTexture failed: %s", SDL_GetError());

    return texture;
}

SDL_GPUTexture*
createSampledColorTarget(SDL_GPUDevice* device, Uint32 width, Uint32 height, SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
    if (!texture)
        SDL_Log("createSampledColorTarget: SDL_CreateGPUTexture failed: %s", SDL_GetError());

    return texture;
}

SDL_GPUSampler* createLinearRepeatSampler(SDL_GPUDevice* device)
{
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;

    return SDL_CreateGPUSampler(device, &samplerInfo);
}

SDL_GPUSampler* createLinearComparisonSampler(SDL_GPUDevice* device, SDL_GPUFilter filterMode)
{
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = filterMode;
    samplerInfo.mag_filter = filterMode;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    samplerInfo.enable_compare = true;
    samplerInfo.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    return SDL_CreateGPUSampler(device, &samplerInfo);
}

SDL_GPUSampler* createLinearClampSampler(SDL_GPUDevice* device)
{
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    return SDL_CreateGPUSampler(device, &samplerInfo);
}

} // namespace Boilerplate
