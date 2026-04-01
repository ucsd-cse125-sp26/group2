#include "Renderer.hpp"

#include <SDL3/SDL.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

// ---------------------------------------------------------------------------
// Shader loading (SPIR-V)
// ---------------------------------------------------------------------------

#ifdef TITANDOOM_BUNDLE_SHADERS
#include "embedded_shaders.hpp"

namespace
{
struct EmbeddedShader
{
    const Uint8* data;
    Uint32 size;
};

static EmbeddedShader findEmbedded(const char* filename) noexcept
{
    if (SDL_strcmp(filename, "scene.vert.spv") == 0)
        return {k_spv_scene_vert_spv, k_spv_scene_vert_spv_size};
    if (SDL_strcmp(filename, "scene.frag.spv") == 0)
        return {k_spv_scene_frag_spv, k_spv_scene_frag_spv_size};
    if (SDL_strcmp(filename, "triangle.vert.spv") == 0)
        return {k_spv_triangle_vert_spv, k_spv_triangle_vert_spv_size};
    if (SDL_strcmp(filename, "triangle.frag.spv") == 0)
        return {k_spv_triangle_frag_spv, k_spv_triangle_frag_spv_size};
    return {nullptr, 0};
}
} // namespace
#endif

SDL_GPUShader* Renderer::loadShader(const char* filename,
                                    SDL_GPUShaderStage stage,
                                    uint32_t vertUniformBufs,
                                    uint32_t fragUniformBufs) const
{
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint          = "main";
    info.format              = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage               = stage;
    info.num_uniform_buffers = (stage == SDL_GPU_SHADERSTAGE_VERTEX) ? vertUniformBufs : fragUniformBufs;

#ifdef TITANDOOM_BUNDLE_SHADERS
    EmbeddedShader emb = findEmbedded(filename);
    if (!emb.data) {
        SDL_Log("Renderer: no embedded shader named '%s'", filename);
        return nullptr;
    }
    info.code      = emb.data;
    info.code_size = emb.size;

    SDL_GPUShader* shader = SDL_CreateGPUShader(gpu, &info);
    if (!shader)
        SDL_Log("Renderer: SDL_CreateGPUShader('%s') failed: %s", filename, SDL_GetError());
    return shader;
#else
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
    info.code      = static_cast<const Uint8*>(code);
    info.code_size = codeSize;

    SDL_GPUShader* shader = SDL_CreateGPUShader(gpu, &info);
    SDL_free(code);
    if (!shader)
        SDL_Log("Renderer: SDL_CreateGPUShader('%s') failed: %s", filename, SDL_GetError());
    return shader;
#endif
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

    SDL_WaitForGPUIdle(gpu);
    return buf;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool Renderer::init(SDL_GPUDevice* gpuDevice, SDL_Window* win, const World& world)
{
    gpu     = gpuDevice;
    window  = win;
    windowW = 0;
    windowH = 0;

    // ---- Shaders — note scene.frag now uses 1 fragment uniform buffer ----
    SDL_GPUShader* vert = loadShader("scene.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader* frag = loadShader("scene.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
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

    SDL_GPUDepthStencilState depthState{};
    depthState.compare_op         = SDL_GPU_COMPAREOP_LESS;
    depthState.enable_depth_test  = true;
    depthState.enable_depth_write = true;

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(gpu, window);

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader                         = vert;
    pipelineInfo.fragment_shader                       = frag;
    pipelineInfo.vertex_input_state                    = vertexInput;
    pipelineInfo.primitive_type                        = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.depth_stencil_state                   = depthState;
    pipelineInfo.rasterizer_state.fill_mode            = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode            = SDL_GPU_CULLMODE_NONE;
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
    if (!worldVBuf)
        return false;

    // ---- Build one player model per slot ----
    for (int i = 0; i < 4; ++i) {
        glm::vec3 col          = MeshGen::k_playerColors[i];
        auto verts             = MeshGen::buildPlayerModel(col);
        playerModels[i].vcount = static_cast<uint32_t>(verts.size());
        playerModels[i].color  = col;
        playerModels[i].vbuf   = uploadVertexBuffer(verts);
        if (!playerModels[i].vbuf)
            SDL_Log("Renderer: player model %d upload failed", i);
    }

    return true;
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
    createDepthTexture(w, h);
}

// ---------------------------------------------------------------------------
// drawScene — world + players
// ---------------------------------------------------------------------------

void Renderer::drawScene(SDL_GPUCommandBuffer* cmdbuf,
                         SDL_GPUTexture* swapchain,
                         const SceneUniforms& viewProjUniforms,
                         const glm::vec3 playerPositions[4],
                         const float playerYaws[4],
                         const bool playerAlive[4],
                         int localPlayerId)
{
    if (!depthTex) {
        SDL_Log("Renderer::drawScene: depthTex is NULL — skipping frame");
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

    // ---- Draw world geometry ----
    // Uniforms MUST be pushed OUTSIDE the render pass.
    SDL_PushGPUVertexUniformData(cmdbuf, 0, &viewProjUniforms, sizeof(viewProjUniforms));

    // White tint for world geometry
    FragTint worldTint{{1.0f, 1.0f, 1.0f, 1.0f}};
    SDL_PushGPUFragmentUniformData(cmdbuf, 0, &worldTint, sizeof(worldTint));

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdbuf, &color, 1, &depth);
    if (!pass) {
        SDL_Log("Renderer::drawScene: SDL_BeginGPURenderPass failed: %s", SDL_GetError());
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    // World
    SDL_GPUBufferBinding wbBind{worldVBuf, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &wbBind, 1);
    SDL_DrawGPUPrimitives(pass, worldVCount, 1, 0, 0);

    // ---- Draw player models ----
    for (int i = 0; i < 4; ++i) {
        if (i == localPlayerId)
            continue; // first-person — skip own model
        if (!playerAlive[i])
            continue;
        if (!playerModels[i].vbuf)
            continue;

        // Build model matrix: translate to world position + rotate by yaw
        glm::mat4 model = glm::mat4(1.0f);
        model           = glm::translate(model, playerPositions[i]);
        model           = glm::rotate(model, playerYaws[i] + glm::radians(180.0f), glm::vec3(0, 1, 0));

        SceneUniforms playerUniforms;
        playerUniforms.viewProj = viewProjUniforms.viewProj;
        playerUniforms.model    = model;

        SDL_EndGPURenderPass(pass);

        SDL_PushGPUVertexUniformData(cmdbuf, 0, &playerUniforms, sizeof(playerUniforms));

        // No tint override — vertex colors carry the team color
        FragTint playerTint{{1.0f, 1.0f, 1.0f, 1.0f}};
        SDL_PushGPUFragmentUniformData(cmdbuf, 0, &playerTint, sizeof(playerTint));

        pass = SDL_BeginGPURenderPass(cmdbuf, &color, 1, &depth);
        if (!pass)
            break;

        // LOAD (not CLEAR) subsequent passes
        color.load_op  = SDL_GPU_LOADOP_LOAD;
        depth.load_op  = SDL_GPU_LOADOP_LOAD;
        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_GPUBufferBinding pbBind{playerModels[i].vbuf, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &pbBind, 1);
        SDL_DrawGPUPrimitives(pass, playerModels[i].vcount, 1, 0, 0);
    }

    if (pass)
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
    for (auto& pm : playerModels) {
        if (pm.vbuf)
            SDL_ReleaseGPUBuffer(gpu, pm.vbuf);
        pm.vbuf = nullptr;
    }
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
