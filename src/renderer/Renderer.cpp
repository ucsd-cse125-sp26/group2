#include "Renderer.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Shader loading (SPIR-V, relative to executable via SDL_GetBasePath)
// ---------------------------------------------------------------------------

SDL_GPUShader* Renderer::loadShader(const char* filename, SDL_GPUShaderStage stage, uint32_t uniformBuffers) const
{
    const char* base = SDL_GetBasePath();
    std::string path = (base ? base : "./");
    path += "shaders/";
    path += filename;

    size_t codeSize = 0;
    void* code      = SDL_LoadFile(path.c_str(), &codeSize);
    if (!code) {
        SDL_Log("Renderer: cannot load shader '%s': %s", path.c_str(), SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code                = static_cast<const Uint8*>(code);
    info.code_size           = codeSize;
    info.entrypoint          = "main";
    info.format              = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage               = stage;
    info.num_uniform_buffers = uniformBuffers;

    SDL_GPUShader* shader = SDL_CreateGPUShader(gpu, &info);
    SDL_free(code);
    if (!shader)
        SDL_Log("Renderer: SDL_CreateGPUShader failed: %s", SDL_GetError());
    return shader;
}

// ---------------------------------------------------------------------------
// Depth texture
// ---------------------------------------------------------------------------

void Renderer::createDepthTexture(uint32_t w, uint32_t h)
{
    if (depthTex)
        SDL_ReleaseGPUTexture(gpu, depthTex);

    SDL_GPUTextureCreateInfo info{};
    info.type                 = SDL_GPU_TEXTURETYPE_2D;
    info.format               = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width                = w;
    info.height               = h;
    info.layer_count_or_depth = 1;
    info.num_levels           = 1;
    info.sample_count         = SDL_GPU_SAMPLECOUNT_1;

    depthTex = SDL_CreateGPUTexture(gpu, &info);
    if (!depthTex)
        SDL_Log("Renderer: cannot create depth texture: %s", SDL_GetError());
}

// ---------------------------------------------------------------------------
// Upload vertex data via a transfer buffer
// ---------------------------------------------------------------------------

SDL_GPUBuffer* Renderer::uploadVertexBuffer(const std::vector<Vertex>& verts) const
{
    if (verts.empty())
        return nullptr;
    uint32_t byteSize = static_cast<uint32_t>(verts.size() * sizeof(Vertex));

    SDL_GPUBufferCreateInfo bufInfo{};
    bufInfo.usage      = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufInfo.size       = byteSize;
    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(gpu, &bufInfo);
    if (!buf) {
        SDL_Log("Renderer: vertex buffer create failed");
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage              = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size               = byteSize;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tbInfo);

    void* mapped = SDL_MapGPUTransferBuffer(gpu, tb, false);
    std::memcpy(mapped, verts.data(), byteSize);
    SDL_UnmapGPUTransferBuffer(gpu, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{tb, 0};
    SDL_GPUBufferRegion dst{buf, 0, byteSize};
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(gpu, tb);

    // Wait for the upload to actually complete before the GPU tries to render
    // (belt-and-suspenders: same-queue ordering should suffice, but be safe here).
    SDL_WaitForGPUIdle(gpu);

    return buf;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool Renderer::init(SDL_GPUDevice* gpuDevice, SDL_Window* win, const World& world)
{
    gpu    = gpuDevice;
    window = win;

    // Depth texture is created lazily on the first real swapchain frame
    // (via onResize), so we don't guess the physical pixel size here.
    // windowW/H of 0 ensures onResize always runs on the first frame.
    windowW = 0;
    windowH = 0;

    // ---- Shaders ----
    SDL_GPUShader* vert = loadShader("scene.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1);
    SDL_GPUShader* frag = loadShader("scene.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0);
    if (!vert || !frag)
        return false;

    // ---- Vertex input layout: vec3 position + vec3 color ----
    SDL_GPUVertexAttribute attrs[2] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, sizeof(float) * 3},
    };
    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot               = 0;
    vbDesc.pitch              = sizeof(Vertex);
    vbDesc.input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbDesc.instance_step_rate = 0;

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_attributes          = attrs;
    vertexInput.num_vertex_attributes      = 2;
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers         = 1;

    // ---- Depth/stencil state ----
    SDL_GPUDepthStencilState depthState{};
    depthState.compare_op         = SDL_GPU_COMPAREOP_LESS;
    depthState.enable_depth_test  = true;
    depthState.enable_depth_write = true;

    // ---- Pipeline ----
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(gpu, window);

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader                         = vert;
    pipelineInfo.fragment_shader                       = frag;
    pipelineInfo.vertex_input_state                    = vertexInput;
    pipelineInfo.primitive_type                        = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.depth_stencil_state                   = depthState;
    pipelineInfo.rasterizer_state.fill_mode            = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode            = SDL_GPU_CULLMODE_NONE; // prototype: no culling
    pipelineInfo.rasterizer_state.front_face           = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets         = 1;
    pipelineInfo.target_info.depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pipelineInfo.target_info.has_depth_stencil_target  = true;

    pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &pipelineInfo);
    SDL_ReleaseGPUShader(gpu, vert);
    SDL_ReleaseGPUShader(gpu, frag);
    if (!pipeline) {
        SDL_Log("Renderer: pipeline create failed: %s", SDL_GetError());
        return false;
    }

    // ---- Upload world mesh ----
    worldVCount = static_cast<uint32_t>(world.mesh.size());
    worldVBuf   = uploadVertexBuffer(world.mesh);

    return worldVBuf != nullptr;
}

// ---------------------------------------------------------------------------
// onResize
// ---------------------------------------------------------------------------

void Renderer::onResize(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0)
        return;
    if (w == windowW && h == windowH)
        return;
    SDL_Log("Renderer: resize %u×%u → %u×%u", windowW, windowH, w, h);
    windowW = w;
    windowH = h;
    // Release old texture; SDL3 frees it when the GPU finishes with it.
    // Do NOT call SDL_WaitForGPUIdle here — this may be called while a command
    // buffer is in flight, and waiting would deadlock on some backends.
    createDepthTexture(w, h);
}

// ---------------------------------------------------------------------------
// drawWorld
// ---------------------------------------------------------------------------

void Renderer::drawWorld(SDL_GPUCommandBuffer* cmdbuf, SDL_GPUTexture* swapchain, const SceneUniforms& uniforms)
{
    if (!depthTex) {
        SDL_Log("Renderer::drawWorld: depthTex is NULL — skipping frame");
        return;
    }

    SDL_GPUColorTargetInfo color{};
    color.texture     = swapchain;
    color.clear_color = {0.05f, 0.07f, 0.12f, 1.0f};
    color.load_op     = SDL_GPU_LOADOP_CLEAR;
    color.store_op    = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture     = depthTex;
    depth.clear_depth = 1.0f;
    depth.load_op     = SDL_GPU_LOADOP_CLEAR;
    depth.store_op    = SDL_GPU_STOREOP_DONT_CARE;

    // SDL3 docs: SDL_PushGPUVertexUniformData MUST be called OUTSIDE any render/copy/compute pass.
    SDL_PushGPUVertexUniformData(cmdbuf, 0, &uniforms, sizeof(uniforms));

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdbuf, &color, 1, &depth);
    if (!pass) {
        SDL_Log("Renderer::drawWorld: SDL_BeginGPURenderPass failed: %s", SDL_GetError());
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    SDL_GPUBufferBinding vbBind{worldVBuf, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);

    SDL_DrawGPUPrimitives(pass, worldVCount, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

// ---------------------------------------------------------------------------
// destroy
// ---------------------------------------------------------------------------

void Renderer::destroy()
{
    if (!gpu)
        return;
    SDL_WaitForGPUIdle(gpu);
    if (worldVBuf)
        SDL_ReleaseGPUBuffer(gpu, worldVBuf);
    if (depthTex)
        SDL_ReleaseGPUTexture(gpu, depthTex);
    if (pipeline)
        SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
    worldVBuf = nullptr;
    depthTex  = nullptr;
    pipeline  = nullptr;
}
