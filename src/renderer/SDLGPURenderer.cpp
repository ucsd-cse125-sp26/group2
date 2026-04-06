#include "SDLGPURenderer.hpp"

#include <SDL3/SDL.h>

// When BUNDLE_SHADERS is ON (Release builds), shader bytes are embedded as
// constexpr arrays in this generated header instead of loaded from disk.
#ifdef BUNDLE_SHADERS
#include "embedded_shaders.hpp"
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
// Load a shader from disk or from the embedded byte arrays.
// shaderBase — path stem without extension, e.g. "shaders/triangle.vert"
// format     — SDL_GPU_SHADERFORMAT_SPIRV or SDL_GPU_SHADERFORMAT_MSL
SDL_GPUShader* loadShader(SDL_GPUDevice* dev,
                          const char* shaderBase,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount)
{
    SDL_GPUShaderCreateInfo info{};
    info.stage = stage;
    info.format = format;
    // SPIR-V entry point is "main"; spirv-cross renames it to "main0" in MSL
    // (Metal forbids a function literally named "main").
    info.entrypoint = (format == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";
    info.num_samplers = samplerCount;
    info.num_uniform_buffers = uniformBufferCount;
    info.num_storage_buffers = storageBufferCount;
    info.num_storage_textures = storageTextureCount;

#ifdef BUNDLE_SHADERS
    // Embedded shaders — select by base name + format.
    // EmbedShaders.cmake generates: k_<stem>_<ext>[] / k_<stem>_<ext>_size
    // where <stem>_<ext> is the filename with non-alphanumeric chars → '_'.
    static const struct
    {
        const char* name;
        SDL_GPUShaderFormat fmt;
        const Uint8* code;
        Uint32 size;
    } k_embedded[] = {
        // SPIR-V — Vulkan backend (Linux, Windows)
        {"shaders/triangle.vert", SDL_GPU_SHADERFORMAT_SPIRV, k_triangle_vert_spv, k_triangle_vert_spv_size},
        {"shaders/triangle.frag", SDL_GPU_SHADERFORMAT_SPIRV, k_triangle_frag_spv, k_triangle_frag_spv_size},
#ifdef HAVE_MSL_SHADERS
        // MSL — Metal backend (macOS)
        {"shaders/triangle.vert", SDL_GPU_SHADERFORMAT_MSL, k_triangle_vert_msl, k_triangle_vert_msl_size},
        {"shaders/triangle.frag", SDL_GPU_SHADERFORMAT_MSL, k_triangle_frag_msl, k_triangle_frag_msl_size},
#endif
    };

    bool found = false;
    for (const auto& e : k_embedded) {
        if (e.fmt == format && SDL_strcmp(e.name, shaderBase) == 0) {
            info.code = e.code;
            info.code_size = e.size;
            found = true;
            break;
        }
    }
    if (!found) {
        SDL_Log("SDLGPURenderer: no embedded shader for '%s' (format 0x%x)", shaderBase, static_cast<unsigned>(format));
        return nullptr;
    }
#else
    // Load shader from disk next to the binary.
    const char* ext = (format == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";
    const char* basePath = SDL_GetBasePath();
    char fullPath[512];
    SDL_snprintf(fullPath, sizeof(fullPath), "%s%s%s", basePath ? basePath : "", shaderBase, ext);

    size_t codeSize = 0;
    void* code = SDL_LoadFile(fullPath, &codeSize);
    if (!code) {
        SDL_Log("SDLGPURenderer: failed to load %s: %s", fullPath, SDL_GetError());
        return nullptr;
    }
    info.code = static_cast<const Uint8*>(code);
    info.code_size = static_cast<Uint32>(codeSize);
#endif

    SDL_GPUShader* shader = SDL_CreateGPUShader(dev, &info);

#ifndef BUNDLE_SHADERS
    SDL_free(const_cast<void*>(static_cast<const void*>(info.code)));
#endif

    if (!shader)
        SDL_Log("SDLGPURenderer: SDL_CreateGPUShader(%s) failed: %s", shaderBase, SDL_GetError());
    return shader;
}
} // namespace

// ---------------------------------------------------------------------------
// IRenderer implementation
// ---------------------------------------------------------------------------

bool SDLGPURenderer::init(SDL_Window* win)
{
    gpuWindow = win;

    // Request every shader format we have compiled shaders for.
    // SDL3 will pick the best available backend (Vulkan → SPIR-V on Linux/Windows,
    // Metal → MSL on macOS).
    constexpr SDL_GPUShaderFormat k_wantedFormats = SDL_GPU_SHADERFORMAT_SPIRV
#ifdef HAVE_MSL_SHADERS
                                                    | SDL_GPU_SHADERFORMAT_MSL
#endif
        ;

    gpuDevice = SDL_CreateGPUDevice(k_wantedFormats, false, nullptr);
    if (!gpuDevice) {
        SDL_Log("SDLGPURenderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("SDLGPURenderer: driver = %s", SDL_GetGPUDeviceDriver(gpuDevice));

    // Determine which single format the chosen backend actually uses.
    const SDL_GPUShaderFormat available = SDL_GetGPUShaderFormats(gpuDevice);
    SDL_GPUShaderFormat activeFormat = SDL_GPU_SHADERFORMAT_INVALID;
    if (available & SDL_GPU_SHADERFORMAT_SPIRV) {
        activeFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    }
#ifdef HAVE_MSL_SHADERS
    else if (available & SDL_GPU_SHADERFORMAT_MSL)
    {
        activeFormat = SDL_GPU_SHADERFORMAT_MSL;
    }
#endif
    if (activeFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("SDLGPURenderer: no supported shader format (available: 0x%x)", static_cast<unsigned>(available));
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(gpuDevice, gpuWindow)) {
        SDL_Log("SDLGPURenderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Load shaders using the active format.
    SDL_GPUShader* vert =
        loadShader(gpuDevice, "shaders/triangle.vert", activeFormat, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 0);
    SDL_GPUShader* frag =
        loadShader(gpuDevice, "shaders/triangle.frag", activeFormat, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(gpuDevice, vert);
        SDL_ReleaseGPUShader(gpuDevice, frag);
        return false;
    }

    // Build the graphics pipeline.
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vert;
    pipelineInfo.fragment_shader = frag;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // No vertex buffers — positions are embedded in the vertex shader via gl_VertexIndex.
    pipelineInfo.vertex_input_state = {};

    // Render target: match the swapchain format.
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(gpuDevice, gpuWindow);
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets = 1;

    // Rasterizer defaults are fine (fill, no cull).
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    gpuPipeline = SDL_CreateGPUGraphicsPipeline(gpuDevice, &pipelineInfo);

    // Shaders are consumed by the pipeline — release our references.
    SDL_ReleaseGPUShader(gpuDevice, vert);
    SDL_ReleaseGPUShader(gpuDevice, frag);

    if (!gpuPipeline) {
        SDL_Log("SDLGPURenderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void SDLGPURenderer::draw(SDL_GPURenderPass* renderPass)
{
    SDL_BindGPUGraphicsPipeline(renderPass, gpuPipeline);
    SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);
}

void SDLGPURenderer::shutdown()
{
    if (gpuDevice) {
        SDL_WaitForGPUIdle(gpuDevice);
        if (gpuPipeline)
            SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuPipeline);
        SDL_ReleaseWindowFromGPUDevice(gpuDevice, gpuWindow);
        SDL_DestroyGPUDevice(gpuDevice);
    }
    gpuPipeline = nullptr;
    gpuDevice = nullptr;
    gpuWindow = nullptr;
}
