#include "Renderer.hpp"

#include <SDL3/SDL.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <algorithm>
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
// Impact markers
// ---------------------------------------------------------------------------

void Renderer::addImpact(const glm::vec3& pos, const glm::vec3& normal)
{
    constexpr float k_life = 0.35f;
    // Recycle oldest if full
    if (static_cast<int>(impacts.size()) >= k_maxImpacts)
        impacts.erase(impacts.begin());
    impacts.push_back({pos, normal, k_life, k_life});
}

void Renderer::tickImpacts(float dt)
{
    for (auto it = impacts.begin(); it != impacts.end();) {
        it->life -= dt;
        if (it->life <= 0.0f)
            it = impacts.erase(it);
        else
            ++it;
    }
}

void Renderer::uploadImpacts(SDL_GPUCommandBuffer* cmdbuf)
{
    impactVCount = 0;
    if (impacts.empty())
        return;

    // Build a small cross of 3 axis-aligned quads at each impact point.
    // Size fades from 8 qu down to 0 as life expires.
    std::vector<Vertex> verts;
    verts.reserve(impacts.size() * 18);

    for (const auto& imp : impacts) {
        float frac   = imp.life / imp.maxLife; // 1 → 0
        float sz     = 8.0f * frac;            // shrink over time
        float bright = frac;
        // Bright yellow-orange spark color
        glm::vec3 col = {1.0f, 0.6f + bright * 0.4f, bright * 0.3f};

        // Three thin axis-aligned crosses centred on impact point:
        // XY cross
        auto q = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
            verts.push_back({a, col});
            verts.push_back({b, col});
            verts.push_back({c, col});
            verts.push_back({a, col});
            verts.push_back({c, col});
            verts.push_back({d, col});
        };
        glm::vec3 p = imp.pos;
        float h     = sz * 0.5f;
        float t     = 1.5f; // thickness
        // Horizontal bar (XZ plane)
        q(p + glm::vec3(-h, -t, -t), p + glm::vec3(h, -t, -t), p + glm::vec3(h, t, t), p + glm::vec3(-h, t, t));
        // Vertical bar (Y axis)
        q(p + glm::vec3(-t, -h, -t), p + glm::vec3(t, -h, -t), p + glm::vec3(t, h, t), p + glm::vec3(-t, h, t));
        // Depth bar (Z axis)
        q(p + glm::vec3(-t, -t, -h), p + glm::vec3(t, -t, -h), p + glm::vec3(t, t, h), p + glm::vec3(-t, t, h));
    }

    if (verts.empty())
        return;

    uint32_t byteSize = static_cast<uint32_t>(verts.size() * sizeof(Vertex));

    // Ensure impactVBuf is large enough; recreate if needed
    if (!impactVBuf || byteSize > k_impactBufVerts * sizeof(Vertex)) {
        if (impactVBuf)
            SDL_ReleaseGPUBuffer(gpu, impactVBuf);
        SDL_GPUBufferCreateInfo bi{};
        bi.usage   = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size    = std::max(byteSize, k_impactBufVerts * static_cast<uint32_t>(sizeof(Vertex)));
        impactVBuf = SDL_CreateGPUBuffer(gpu, &bi);
    }

    if (!impactVBuf)
        return;

    SDL_GPUTransferBufferCreateInfo tbCI{};
    tbCI.usage                = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbCI.size                 = byteSize;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tbCI);
    if (!tb)
        return;

    void* mapped = SDL_MapGPUTransferBuffer(gpu, tb, false);
    std::memcpy(mapped, verts.data(), byteSize);
    SDL_UnmapGPUTransferBuffer(gpu, tb);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmdbuf);
    SDL_GPUTransferBufferLocation src{tb, 0};
    SDL_GPUBufferRegion dst{impactVBuf, 0, byteSize};
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu, tb);

    impactVCount = static_cast<uint32_t>(verts.size());
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

    // Upload dynamic impact marker geometry before any render pass opens.
    uploadImpacts(cmdbuf);

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

    // World geometry
    SDL_GPUBufferBinding wbBind{worldVBuf, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &wbBind, 1);
    SDL_DrawGPUPrimitives(pass, worldVCount, 1, 0, 0);

    // Impact markers (uploaded CPU-side each frame, rendered in same pass)
    if (impactVBuf && impactVCount > 0) {
        SceneUniforms identityUniforms;
        identityUniforms.viewProj = viewProjUniforms.viewProj;
        identityUniforms.model    = glm::mat4(1.0f);
        // Must end and re-open to push new vertex uniforms outside pass
        SDL_EndGPURenderPass(pass);

        color.load_op  = SDL_GPU_LOADOP_LOAD;
        depth.load_op  = SDL_GPU_LOADOP_LOAD;
        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_PushGPUVertexUniformData(cmdbuf, 0, &identityUniforms, sizeof(identityUniforms));
        FragTint impTint{{1.0f, 1.0f, 1.0f, 1.0f}};
        SDL_PushGPUFragmentUniformData(cmdbuf, 0, &impTint, sizeof(impTint));

        pass = SDL_BeginGPURenderPass(cmdbuf, &color, 1, &depth);
        if (pass) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_GPUBufferBinding ibBind{impactVBuf, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &ibBind, 1);
            SDL_DrawGPUPrimitives(pass, impactVCount, 1, 0, 0);
        }
    }

    // ---- Draw player models ----
    // CRITICAL: set load_op = LOAD BEFORE the first player-model pass begins,
    // otherwise BeginGPURenderPass clears the world geometry we just drew.
    color.load_op  = SDL_GPU_LOADOP_LOAD;
    depth.load_op  = SDL_GPU_LOADOP_LOAD;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;

    for (int i = 0; i < 4; ++i) {
        if (i == localPlayerId || !playerAlive[i] || !playerModels[i].vbuf)
            continue;

        glm::mat4 model = glm::mat4(1.0f);
        model           = glm::translate(model, playerPositions[i]);
        model           = glm::rotate(model, playerYaws[i] + glm::radians(180.0f), glm::vec3(0, 1, 0));

        SceneUniforms playerUniforms;
        playerUniforms.viewProj = viewProjUniforms.viewProj;
        playerUniforms.model    = model;

        // Uniforms MUST be pushed outside a render pass.
        if (pass) {
            SDL_EndGPURenderPass(pass);
            pass = nullptr;
        }

        SDL_PushGPUVertexUniformData(cmdbuf, 0, &playerUniforms, sizeof(playerUniforms));
        FragTint playerTint{{1.0f, 1.0f, 1.0f, 1.0f}};
        SDL_PushGPUFragmentUniformData(cmdbuf, 0, &playerTint, sizeof(playerTint));

        pass = SDL_BeginGPURenderPass(cmdbuf, &color, 1, &depth);
        if (!pass)
            break;

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
