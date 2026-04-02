#include "SDLGPURenderer.hpp"

#include <SDL3/SDL.h>

#include <glm/vec2.hpp>

// When BUNDLE_SHADERS is ON (Release builds), SPIR-V bytes are embedded as
// constexpr arrays in this generated header instead of loaded from disk.
#ifdef BUNDLE_SHADERS
#include "embedded_shaders.hpp"
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
// Load a SPIR-V shader from disk or from the embedded byte arrays.
SDL_GPUShader* loadShader(SDL_GPUDevice* dev,
                          const char* spvPath,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount)
{
    SDL_GPUShaderCreateInfo info{};
    info.stage = stage;
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.entrypoint = "main";
    info.num_samplers = samplerCount;
    info.num_uniform_buffers = uniformBufferCount;
    info.num_storage_buffers = storageBufferCount;
    info.num_storage_textures = storageTextureCount;

#ifdef BUNDLE_SHADERS
    // Embedded SPIR-V — pick the right array by filename stem.
    // EmbedShaders.cmake generates: k_spv_<stem>_<ext>_spv[] / _size
    if (SDL_strcmp(spvPath, "shaders/triangle.vert.spv") == 0) {
        info.code = k_spv_triangle_vert_spv;
        info.code_size = k_spv_triangle_vert_spv_size;
    } else if (SDL_strcmp(spvPath, "shaders/triangle.frag.spv") == 0) {
        info.code = k_spv_triangle_frag_spv;
        info.code_size = k_spv_triangle_frag_spv_size;
    } else {
        SDL_Log("SDLGPURenderer: unknown embedded shader: %s", spvPath);
        return nullptr;
    }
#else
    // Load SPIR-V from disk next to the binary.
    const char* basePath = SDL_GetBasePath();
    char fullPath[512];
    SDL_snprintf(fullPath, sizeof(fullPath), "%s%s", basePath ? basePath : "", spvPath);

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
        SDL_Log("SDLGPURenderer: SDL_CreateGPUShader(%s) failed: %s", spvPath, SDL_GetError());
    return shader;
}
} // namespace

// ---------------------------------------------------------------------------
// IRenderer implementation
// ---------------------------------------------------------------------------

bool SDLGPURenderer::init(SDL_Window* win)
{
    window = win;

    // Create a GPU device — prefer Vulkan, fall back to whatever SDL3 picks.
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (!device) {
        SDL_Log("SDLGPURenderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("SDLGPURenderer: driver = %s", SDL_GetGPUDeviceDriver(device));

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("SDLGPURenderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Load shaders.
    // Vertex shader uses 1 uniform buffer slot (player position offset).
    SDL_GPUShader* vert = loadShader(device, "shaders/triangle.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader* frag = loadShader(device, "shaders/triangle.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
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
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets = 1;

    // Rasterizer defaults are fine (fill, no cull).
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

    // Shaders are consumed by the pipeline — release our references.
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!pipeline) {
        SDL_Log("SDLGPURenderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void SDLGPURenderer::renderFrame(glm::vec2 playerPos)
{
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
        return;

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h) || !swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapchain;
    colorTarget.clear_color = {.r = 0.10f, .g = 0.10f, .b = 0.10f, .a = 1.0f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    // Upload player position as a vertex uniform (slot 0 → set 1, binding 0 in the shader).
    SDL_PushGPUVertexUniformData(cmd, 0, &playerPos, sizeof(playerPos));

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0); // 3 verts, 1 instance
    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
}

void SDLGPURenderer::shutdown()
{
    if (device) {
        SDL_WaitForGPUIdle(device);
        if (pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }
    pipeline = nullptr;
    device = nullptr;
    window = nullptr;
}
