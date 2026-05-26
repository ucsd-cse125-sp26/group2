/// @file HudRenderer.cpp
/// @brief GPU backend implementation for the HUD system.

#include "HudRenderer.hpp"

#include "particles/sdf/SdfAtlas.hpp"
#include "renderer-new/ShaderUtils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

// ── Init / Quit ─────────────────────────────────────────────────────────────

bool HudRenderer::init(SDL_GPUDevice* device,
                       SDL_GPUShaderFormat shaderFormat,
                       const SdfAtlas& sdfAtlas,
                       uint32_t screenW,
                       uint32_t screenH)
{
    device_ = device;
    shaderFormat_ = shaderFormat;
    sdfAtlasTex_ = sdfAtlas.gpuTexture();
    sdfAtlasSamp_ = sdfAtlas.gpuSampler();

    if (!createOffscreenTarget(screenW, screenH))
        return false;

    if (!createPipeline())
        return false;

    // Create a 1x1 white fallback icon atlas (replaced when real atlas is loaded).
    {
        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.width = 1;
        tci.height = 1;
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        iconAtlasTex_ = SDL_CreateGPUTexture(device_, &tci);
        if (!iconAtlasTex_) {
            SDL_Log("HudRenderer: failed to create fallback icon texture");
            return false;
        }

        // Upload white pixel.
        const uint8_t white[4] = {255, 255, 255, 255};
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = 4;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &tbci);
        void* mapped = SDL_MapGPUTransferBuffer(device_, tb, false);
        std::memcpy(mapped, white, 4);
        SDL_UnmapGPUTransferBuffer(device_, tb);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        SDL_GPUTextureRegion dst{};
        dst.texture = iconAtlasTex_;
        dst.w = 1;
        dst.h = 1;
        dst.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, tb);

        // Sampler
        SDL_GPUSamplerCreateInfo sci{};
        sci.min_filter = SDL_GPU_FILTER_LINEAR;
        sci.mag_filter = SDL_GPU_FILTER_LINEAR;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        iconAtlasSamp_ = SDL_CreateGPUSampler(device_, &sci);
    }

    SDL_Log("HudRenderer: init OK (%ux%u)", screenW, screenH);
    return true;
}

void HudRenderer::quit()
{
    if (!device_)
        return;

    SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    SDL_ReleaseGPUGraphicsPipeline(device_, erasePipeline_);
    SDL_ReleaseGPUTexture(device_, msaaTarget_);
    SDL_ReleaseGPUTexture(device_, offscreenTarget_);
    SDL_ReleaseGPUTexture(device_, iconAtlasTex_);
    SDL_ReleaseGPUSampler(device_, iconAtlasSamp_);
    SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer_);

    pipeline_ = nullptr;
    erasePipeline_ = nullptr;
    msaaTarget_ = nullptr;
    offscreenTarget_ = nullptr;
    iconAtlasTex_ = nullptr;
    iconAtlasSamp_ = nullptr;
    vertexBuffer_ = nullptr;
    transferBuffer_ = nullptr;
    device_ = nullptr;
}

// ── Resize ──────────────────────────────────────────────────────────────────

void HudRenderer::resize(uint32_t newW, uint32_t newH)
{
    if (newW == width_ && newH == height_)
        return;
    SDL_ReleaseGPUTexture(device_, offscreenTarget_);
    SDL_ReleaseGPUTexture(device_, msaaTarget_);
    offscreenTarget_ = nullptr;
    msaaTarget_ = nullptr;
    createOffscreenTarget(newW, newH);
}

// ── Render ──────────────────────────────────────────────────────────────────

void HudRenderer::render(std::span<const HudVertex> vertices, std::span<const std::array<float, 6>> clipRects)
{
    if (vertices.empty() || !pipeline_ || !erasePipeline_ || !offscreenTarget_)
        return;

    const uint32_t vertexCount = static_cast<uint32_t>(vertices.size());

    if (!ensureVertexBuffer(vertexCount))
        return;

    // Upload vertices via transfer buffer.
    {
        void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer_, true);
        std::memcpy(mapped, vertices.data(), vertexCount * sizeof(HudVertex));
        SDL_UnmapGPUTransferBuffer(device_, transferBuffer_);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);

    // Copy transfer -> vertex buffer.
    {
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = transferBuffer_;
        SDL_GPUBufferRegion dst{};
        dst.buffer = vertexBuffer_;
        dst.size = vertexCount * sizeof(HudVertex);
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
        SDL_EndGPUCopyPass(cp);
    }

    // Begin render pass -> MSAA target with end-of-pass auto-resolve into
    // the 1× sampleable target. The resolve happens inside the GPU at zero
    // CPU cost; the swapchain blit only ever sees the resolved 1× texture.
    SDL_GPUColorTargetInfo ct{};
    ct.texture = msaaTarget_;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.clear_color = {0.f, 0.f, 0.f, 0.f}; // transparent black
    ct.store_op = SDL_GPU_STOREOP_RESOLVE; // resolve MSAA -> single-sample, discard MSAA contents
    ct.resolve_texture = offscreenTarget_;
    ct.resolve_layer = 0;
    ct.resolve_mip_level = 0;
    ct.cycle = false;
    ct.cycle_resolve_texture = false;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);

    SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
    const auto bindPipeline = [&](SDL_GPUGraphicsPipeline* pipeline) {
        if (boundPipeline != pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            boundPipeline = pipeline;
        }
    };
    bindPipeline(pipeline_);

    // Bind vertex buffer.
    SDL_GPUBufferBinding vbBinding{};
    vbBinding.buffer = vertexBuffer_;
    SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

    // Push screen-size uniform (vertex UBO slot 0).
    ScreenUniforms su{};
    su.screenW = static_cast<float>(width_);
    su.screenH = static_cast<float>(height_);
    SDL_PushGPUVertexUniformData(cmd, 0, &su, sizeof(su));

    // Bind fragment samplers (set 2: sdfAtlas + iconAtlas).
    SDL_GPUTextureSamplerBinding samplers[2] = {
        {.texture = sdfAtlasTex_, .sampler = sdfAtlasSamp_},
        {.texture = iconAtlasTex_, .sampler = iconAtlasSamp_},
    };
    SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);

    const auto isEraseVertex = [&](uint32_t vertexIndex) {
        return vertexIndex < vertexCount && static_cast<int>(vertices[vertexIndex].texMode + 0.5f) == 6;
    };

    const auto drawSpan = [&](uint32_t startVtx, uint32_t vtxCount) {
        if (startVtx >= vertexCount)
            return;

        uint32_t cursor = startVtx;
        const uint32_t endVtx = startVtx + std::min(vtxCount, vertexCount - startVtx);
        while (cursor < endVtx) {
            const bool erase = isEraseVertex(cursor);
            const uint32_t subStart = cursor++;
            while (cursor < endVtx && isEraseVertex(cursor) == erase)
                ++cursor;

            bindPipeline(erase ? erasePipeline_ : pipeline_);
            SDL_DrawGPUPrimitives(pass, cursor - subStart, 1, subStart, 0);
        }
    };

    // Draw with optional scissor rects.
    if (clipRects.empty()) {
        // No clipping — single draw call.
        drawSpan(0, vertexCount);
    } else {
        for (const auto& cr : clipRects) {
            const uint32_t startVtx = static_cast<uint32_t>(cr[0]);
            const uint32_t vtxCount = static_cast<uint32_t>(cr[1]);

            // cr[2..5] = x, y, w, h.  Negative w means "no scissor" (full viewport).
            if (cr[4] >= 0.f) {
                SDL_Rect scissor{};
                scissor.x = static_cast<int>(cr[2]);
                scissor.y = static_cast<int>(cr[3]);
                scissor.w = static_cast<int>(cr[4]);
                scissor.h = static_cast<int>(cr[5]);
                SDL_SetGPUScissor(pass, &scissor);
            } else {
                SDL_Rect fullVP{0, 0, static_cast<int>(width_), static_cast<int>(height_)};
                SDL_SetGPUScissor(pass, &fullVP);
            }
            drawSpan(startVtx, vtxCount);
        }
    }

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

// ── Internals ───────────────────────────────────────────────────────────────

bool HudRenderer::createOffscreenTarget(uint32_t w, uint32_t h)
{
    // 4× MSAA color target — drawn into, never sampled. SDL_GPU requires
    // the multisample texture and the resolve target be separate resources;
    // the resolve target is the 1× sampleable copy that downstream code
    // (renderer's HUD blit) reads from.
    SDL_GPUTextureCreateInfo msaaInfo{};
    msaaInfo.type = SDL_GPU_TEXTURETYPE_2D;
    msaaInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    msaaInfo.width = w;
    msaaInfo.height = h;
    msaaInfo.layer_count_or_depth = 1;
    msaaInfo.num_levels = 1;
    msaaInfo.sample_count = k_sampleCount;
    msaaInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    msaaTarget_ = SDL_CreateGPUTexture(device_, &msaaInfo);
    if (!msaaTarget_) {
        SDL_Log("HudRenderer: failed to create %dx MSAA target (%ux%u): %s",
                static_cast<int>(k_sampleCount),
                w,
                h,
                SDL_GetError());
        return false;
    }

    // 1× sampleable resolve target — the HUD blit pipeline samples this.
    SDL_GPUTextureCreateInfo resInfo{};
    resInfo.type = SDL_GPU_TEXTURETYPE_2D;
    resInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    resInfo.width = w;
    resInfo.height = h;
    resInfo.layer_count_or_depth = 1;
    resInfo.num_levels = 1;
    resInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    resInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    offscreenTarget_ = SDL_CreateGPUTexture(device_, &resInfo);
    if (!offscreenTarget_) {
        SDL_Log("HudRenderer: failed to create resolve target (%ux%u): %s", w, h, SDL_GetError());
        SDL_ReleaseGPUTexture(device_, msaaTarget_);
        msaaTarget_ = nullptr;
        return false;
    }
    width_ = w;
    height_ = h;
    return true;
}

bool HudRenderer::createPipeline()
{
    SDL_GPUShader* vert = loadShader("hud.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* frag = loadShader("hud.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device_, vert);
        SDL_ReleaseGPUShader(device_, frag);
        return false;
    }

    // Vertex layout: 5 attributes, 48 bytes stride.
    const SDL_GPUVertexBufferDescription vbDesc{
        .slot = 0,
        .pitch = sizeof(HudVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    const SDL_GPUVertexAttribute attrs[5] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = offsetof(HudVertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = offsetof(HudVertex, uv)},
        {.location = 2,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = offsetof(HudVertex, color)},
        {.location = 3,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
         .offset = offsetof(HudVertex, texMode)},
        {.location = 4,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset = offsetof(HudVertex, shapeData)},
    };

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = attrs;
    vertexInput.num_vertex_attributes = 5;

    // Premultiplied-alpha blending.  The fragment shader emits (rgb·a, a)
    // so we pass src color through unmodified (`ONE`) and let blending take
    // care of the rest.  Premultiplied alpha is also a hard requirement for
    // correct MSAA resolve — averaging straight-alpha sub-samples produces
    // dark fringes around glyph edges; premultiplied averages cleanly.
    SDL_GPUColorTargetDescription normalCtDesc{};
    normalCtDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    normalCtDesc.blend_state.enable_blend = true;
    normalCtDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    normalCtDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    normalCtDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    normalCtDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    normalCtDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    normalCtDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUColorTargetDescription eraseCtDesc = normalCtDesc;
    eraseCtDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    eraseCtDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    eraseCtDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    eraseCtDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

    const auto createWithTarget = [&](const SDL_GPUColorTargetDescription& targetDesc) {
        SDL_GPUGraphicsPipelineCreateInfo pci{};
        pci.vertex_shader = vert;
        pci.fragment_shader = frag;
        pci.vertex_input_state = vertexInput;
        pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pci.target_info.color_target_descriptions = &targetDesc;
        pci.target_info.num_color_targets = 1;
        pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        // 4× MSAA sample-count must match the multisample target we render into.
        pci.multisample_state.sample_count = k_sampleCount;
        return SDL_CreateGPUGraphicsPipeline(device_, &pci);
    };

    pipeline_ = createWithTarget(normalCtDesc);
    erasePipeline_ = createWithTarget(eraseCtDesc);
    SDL_ReleaseGPUShader(device_, vert);
    SDL_ReleaseGPUShader(device_, frag);

    if (!pipeline_ || !erasePipeline_) {
        SDL_Log("HudRenderer: pipeline creation failed: %s", SDL_GetError());
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, erasePipeline_);
        pipeline_ = nullptr;
        erasePipeline_ = nullptr;
        return false;
    }
    return true;
}

bool HudRenderer::ensureVertexBuffer(uint32_t requiredVertices)
{
    if (requiredVertices <= vertexCapacity_)
        return true;

    // Round up to next power of 2 (min 256 vertices).
    uint32_t newCap = 256;
    while (newCap < requiredVertices)
        newCap *= 2;

    SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer_);

    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size = newCap * sizeof(HudVertex);
    vertexBuffer_ = SDL_CreateGPUBuffer(device_, &bci);

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = newCap * sizeof(HudVertex);
    transferBuffer_ = SDL_CreateGPUTransferBuffer(device_, &tbci);

    if (!vertexBuffer_ || !transferBuffer_) {
        SDL_Log("HudRenderer: failed to create vertex buffer (%u verts)", newCap);
        return false;
    }
    vertexCapacity_ = newCap;
    return true;
}

SDL_GPUShader*
HudRenderer::loadShader(const char* name, SDL_GPUShaderStage stage, uint32_t samplerCount, uint32_t uniformBufferCount)
{
    const char* base = SDL_GetBasePath();
    const char* ext = (shaderFormat_ == SDL_GPU_SHADERFORMAT_MSL)    ? ".msl"
                      : (shaderFormat_ == SDL_GPU_SHADERFORMAT_DXIL) ? ".dxil"
                                                                     : ".spv";
    char path[512];
    SDL_snprintf(path, sizeof(path), "%sshaders/%s%s", base ? base : "", name, ext);

    return ::loadShader(device_, path, shaderFormat_, stage, samplerCount, uniformBufferCount, 0, 0);
}
