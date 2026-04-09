#include "ParticleRenderer.hpp"

#include "renderer/ShaderUtils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// Blend state helpers
// ---------------------------------------------------------------------------

SDL_GPUColorTargetBlendState ParticleRenderer::additiveBlend()
{
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    return b;
}

SDL_GPUColorTargetBlendState ParticleRenderer::premulAlphaBlend()
{
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    return b;
}

SDL_GPUColorTargetBlendState ParticleRenderer::alphaBlend()
{
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    return b;
}

// ---------------------------------------------------------------------------
// Pipeline factories
// ---------------------------------------------------------------------------

SDL_GPUGraphicsPipeline* ParticleRenderer::makeStoragePipeline(const char* vertName,
                                                               const char* fragName,
                                                               uint32_t storageBufs,
                                                               uint32_t samplers,
                                                               SDL_GPUColorTargetBlendState blend,
                                                               bool depthTest,
                                                               bool depthWrite,
                                                               bool depthBias,
                                                               SDL_GPUPrimitiveType prim)
{
    const char* base = SDL_GetBasePath();
    const char* ext = (shaderFmt_ == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";
    char vp[512], fp[512];
    SDL_snprintf(vp, sizeof(vp), "%sshaders/%s%s", base ? base : "", vertName, ext);
    SDL_snprintf(fp, sizeof(fp), "%sshaders/%s%s", base ? base : "", fragName, ext);

    SDL_GPUShader* vert = loadShader(device_, vp, shaderFmt_, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, storageBufs, 0);
    SDL_GPUShader* frag = loadShader(device_, fp, shaderFmt_, SDL_GPU_SHADERSTAGE_FRAGMENT, samplers, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device_, vert);
        SDL_ReleaseGPUShader(device_, frag);
        return nullptr;
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = colorFmt_;
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = prim;

    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    pci.depth_stencil_state.enable_depth_test = depthTest;
    pci.depth_stencil_state.enable_depth_write = depthWrite;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    if (depthBias) {
        pci.rasterizer_state.enable_depth_bias = true;
        pci.rasterizer_state.depth_bias_constant_factor = -1.0f;
        pci.rasterizer_state.depth_bias_slope_factor = -1.0f;
    }

    SDL_GPUGraphicsPipeline* pl = SDL_CreateGPUGraphicsPipeline(device_, &pci);
    SDL_ReleaseGPUShader(device_, vert);
    SDL_ReleaseGPUShader(device_, frag);
    if (!pl)
        SDL_Log("ParticleRenderer: pipeline %s/%s failed: %s", vertName, fragName, SDL_GetError());
    return pl;
}

SDL_GPUGraphicsPipeline* ParticleRenderer::makeVertexPipeline(const char* vertName,
                                                              const char* fragName,
                                                              SDL_GPUVertexInputState vertexInput,
                                                              SDL_GPUColorTargetBlendState blend,
                                                              bool depthTest,
                                                              bool depthWrite,
                                                              SDL_GPUPrimitiveType prim)
{
    const char* base = SDL_GetBasePath();
    const char* ext = (shaderFmt_ == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";
    char vp[512], fp[512];
    SDL_snprintf(vp, sizeof(vp), "%sshaders/%s%s", base ? base : "", vertName, ext);
    SDL_snprintf(fp, sizeof(fp), "%sshaders/%s%s", base ? base : "", fragName, ext);

    SDL_GPUShader* vert = loadShader(device_, vp, shaderFmt_, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader* frag = loadShader(device_, fp, shaderFmt_, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device_, vert);
        SDL_ReleaseGPUShader(device_, frag);
        return nullptr;
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = colorFmt_;
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = prim;
    pci.vertex_input_state = vertexInput;

    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    pci.depth_stencil_state.enable_depth_test = depthTest;
    pci.depth_stencil_state.enable_depth_write = depthWrite;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    SDL_GPUGraphicsPipeline* pl = SDL_CreateGPUGraphicsPipeline(device_, &pci);
    SDL_ReleaseGPUShader(device_, vert);
    SDL_ReleaseGPUShader(device_, frag);
    if (!pl)
        SDL_Log("ParticleRenderer: vertex pipeline %s/%s failed: %s", vertName, fragName, SDL_GetError());
    return pl;
}

// ---------------------------------------------------------------------------
// Quad index buffer {0,1,2, 2,3,0} × k_maxQuadInstances
// ---------------------------------------------------------------------------

void ParticleRenderer::buildQuadIndexBuffer()
{
    const uint32_t totalIndices = k_maxQuadInstances * 6;
    const uint32_t bytes = totalIndices * sizeof(uint16_t);

    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bi.size = bytes;
    quadIndexBuf_ = SDL_CreateGPUBuffer(device_, &bi);
    if (!quadIndexBuf_) {
        SDL_Log("ParticleRenderer: index buffer alloc failed: %s", SDL_GetError());
        return;
    }

    // Upload via transfer buffer
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    if (!tb)
        return;

    auto* indices = static_cast<uint16_t*>(SDL_MapGPUTransferBuffer(device_, tb, false));
    for (uint32_t i = 0; i < k_maxQuadInstances; ++i) {
        const uint16_t base = static_cast<uint16_t>(i * 4);
        indices[i * 6 + 0] = base + 0;
        indices[i * 6 + 1] = base + 1;
        indices[i * 6 + 2] = base + 2;
        indices[i * 6 + 3] = base + 2;
        indices[i * 6 + 4] = base + 3;
        indices[i * 6 + 5] = base + 0;
    }
    SDL_UnmapGPUTransferBuffer(device_, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    src.offset = 0;

    SDL_GPUBufferRegion dst{};
    dst.buffer = quadIndexBuf_;
    dst.offset = 0;
    dst.size = bytes;

    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, tb);
}

// ---------------------------------------------------------------------------
// Smoke noise texture (procedural 256×256 R8_UNORM value noise)
// ---------------------------------------------------------------------------

void ParticleRenderer::buildSmokeNoise()
{
    constexpr int W = 256, H = 256;
    std::vector<uint8_t> pixels(W * H);

    // Simple tileable value noise using a hash function
    auto hash = [](uint32_t x, uint32_t y) -> float {
        uint32_t h = x * 1664525u + y * 214013u + 2531011u;
        h ^= (h >> 16);
        h *= 0x45d9f3bu;
        h ^= (h >> 16);
        return static_cast<float>(h & 0xFFFFu) / 65535.f;
    };
    // Bilinear interpolation of grid noise
    auto smoothNoise = [&](float fx, float fy) -> float {
        const int x0 = static_cast<int>(fx) & (W - 1);
        const int y0 = static_cast<int>(fy) & (H - 1);
        const int x1 = (x0 + 1) & (W - 1);
        const int y1 = (y0 + 1) & (H - 1);
        const float tx = fx - std::floor(fx);
        const float ty = fy - std::floor(fy);
        const float sx = tx * tx * (3.f - 2.f * tx); // smoothstep
        const float sy = ty * ty * (3.f - 2.f * ty);
        const float v00 = hash(x0, y0);
        const float v10 = hash(x1, y0);
        const float v01 = hash(x0, y1);
        const float v11 = hash(x1, y1);
        return v00 + sx * (v10 - v00) + sy * (v01 - v00) + sx * sy * (v00 - v10 - v01 + v11);
    };

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float n = smoothNoise(x * 0.0625f, y * 0.0625f) * 0.50f;
            n += smoothNoise(x * 0.125f, y * 0.125f) * 0.25f;
            n += smoothNoise(x * 0.25f, y * 0.25f) * 0.125f;
            n += smoothNoise(x * 0.5f, y * 0.5f) * 0.0625f;
            pixels[y * W + x] = static_cast<uint8_t>(std::clamp(n, 0.f, 1.f) * 255.f);
        }
    }

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = W;
    tci.height = H;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    smokeNoise_ = SDL_CreateGPUTexture(device_, &tci);
    if (!smokeNoise_) {
        SDL_Log("ParticleRenderer: smokeNoise texture failed: %s", SDL_GetError());
        return;
    }

    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = W * H;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    void* mapped = SDL_MapGPUTransferBuffer(device_, tb, false);
    std::memcpy(mapped, pixels.data(), W * H);
    SDL_UnmapGPUTransferBuffer(device_, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset = 0;
    src.pixels_per_row = W;
    src.rows_per_layer = H;

    SDL_GPUTextureRegion dst{};
    dst.texture = smokeNoise_;
    dst.w = W;
    dst.h = H;
    dst.d = 1;

    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, tb);

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    smokeSampler_ = SDL_CreateGPUSampler(device_, &sci);
}

// ---------------------------------------------------------------------------
// Bullet hole decal texture (R8G8B8A8, 128×128, procedural)
// ---------------------------------------------------------------------------

void ParticleRenderer::buildDecalTexture()
{
    constexpr int W = 128, H = 128;
    // RGBA8: 4 bytes per pixel
    std::vector<uint8_t> pixels(W * H * 4, 0);

    const float cx = W * 0.5f;
    const float cy = H * 0.5f;
    const float outerR = W * 0.46f;  // outer edge of scorch fade
    const float scorchR = W * 0.34f; // inner scorch / crater rim
    const float craterR = W * 0.22f; // bullet hole proper
    const float holeR = W * 0.14f;   // solid black centre

    // Simple integer hash for procedural roughness
    auto hash = [](int x, int y) -> float {
        uint32_t h = static_cast<uint32_t>(x) * 1664525u + static_cast<uint32_t>(y) * 214013u + 2531011u;
        h ^= (h >> 16);
        h *= 0x45d9f3bu;
        h ^= (h >> 16);
        return static_cast<float>(h & 0xFFu) / 255.f;
    };
    // 3-sample smooth noise (very cheap)
    auto noise = [&](float fx, float fy) -> float {
        return (hash(static_cast<int>(fx), static_cast<int>(fy)) +
                hash(static_cast<int>(fx) + 1, static_cast<int>(fy)) +
                hash(static_cast<int>(fx), static_cast<int>(fy) + 1)) *
               (1.f / 3.f);
    };

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float r = std::sqrt(dx * dx + dy * dy);
            // Procedural roughness displaces the radius boundary so edges look
            // irregular rather than perfectly circular
            const float n = noise(static_cast<float>(x) * 0.35f, static_cast<float>(y) * 0.35f);
            const float roughR = r - n * 5.f; // wiggle up to 5 px

            uint8_t* p = &pixels[(y * W + x) * 4];

            if (roughR > outerR) {
                // Fully transparent outside the decal circle
                p[0] = p[1] = p[2] = p[3] = 0;

            } else if (roughR < holeR) {
                // Solid black bullet hole
                const uint8_t dark = static_cast<uint8_t>(8 + n * 12);
                p[0] = dark;
                p[1] = dark;
                p[2] = dark;
                p[3] = 240;

            } else if (roughR < craterR) {
                // Crater: very dark grey with subtle radial gradient
                const float t = (roughR - holeR) / (craterR - holeR);
                const uint8_t col = static_cast<uint8_t>(15 + t * 25 + n * 20);
                p[0] = col;
                p[1] = static_cast<uint8_t>(col * 0.9f);
                p[2] = static_cast<uint8_t>(col * 0.8f);
                p[3] = 230;

            } else if (roughR < scorchR) {
                // Scorch ring: dark brown/grey with heat tint
                const float t = (roughR - craterR) / (scorchR - craterR);
                const uint8_t r8 = static_cast<uint8_t>(30 + t * 45 + n * 25);
                const uint8_t g8 = static_cast<uint8_t>(22 + t * 32 + n * 18);
                const uint8_t b8 = static_cast<uint8_t>(18 + t * 24 + n * 14);
                p[0] = r8;
                p[1] = g8;
                p[2] = b8;
                p[3] = static_cast<uint8_t>(200 + t * 30);

            } else {
                // Outer scorch fade — chips and soot, alpha tapers to 0
                const float t = (roughR - scorchR) / (outerR - scorchR);
                const float alpha = (1.f - t) * (0.6f + n * 0.4f);
                const uint8_t col = static_cast<uint8_t>(55 + n * 40);
                p[0] = col;
                p[1] = static_cast<uint8_t>(col * 0.8f);
                p[2] = static_cast<uint8_t>(col * 0.65f);
                p[3] = static_cast<uint8_t>(alpha * 180.f);
            }
        }
    }

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = W;
    tci.height = H;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    decalTex_ = SDL_CreateGPUTexture(device_, &tci);
    if (!decalTex_) {
        SDL_Log("ParticleRenderer: decal texture creation failed: %s", SDL_GetError());
        return;
    }

    const uint32_t byteCount = W * H * 4;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = byteCount;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    void* mapped = SDL_MapGPUTransferBuffer(device_, tb, false);
    std::memcpy(mapped, pixels.data(), byteCount);
    SDL_UnmapGPUTransferBuffer(device_, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset = 0;
    src.pixels_per_row = W;
    src.rows_per_layer = H;

    SDL_GPUTextureRegion dst{};
    dst.texture = decalTex_;
    dst.w = W;
    dst.h = H;
    dst.d = 1;

    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, tb);

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    decalSamp_ = SDL_CreateGPUSampler(device_, &sci);
}

// ---------------------------------------------------------------------------
// init / quit
// ---------------------------------------------------------------------------

bool ParticleRenderer::init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt)
{
    device_ = dev;
    colorFmt_ = colorFmt;
    shaderFmt_ = shaderFmt;

    // --- GPU buffers ---
    billboardBuf_.init(dev, sizeof(BillboardParticle) * 4096);
    tracerBuf_.init(dev, sizeof(TracerParticle) * 512); // 80 B × 512 = 40 KB
    ribbonBuf_.init(dev, sizeof(RibbonVertex) * 24576, GpuParticleBuffer::Mode::Vertex);
    hitscanBuf_.init(dev, sizeof(HitscanBeam) * 64);
    arcBuf_.init(dev, sizeof(ArcVertex) * 2048, GpuParticleBuffer::Mode::Vertex);
    smokeBuf_.init(dev, sizeof(SmokeParticle) * 1024); // 48 B × 1024 = 48 KB
    decalBuf_.init(dev, sizeof(DecalInstance) * 512);
    sdfWorldBuf_.init(dev, sizeof(SdfGlyphGPU) * 4096);
    sdfHudBuf_.init(dev, sizeof(SdfGlyphGPU) * 4096);

    buildQuadIndexBuffer();
    buildSmokeNoise();
    buildDecalTexture();

    return buildPipelines();
}

bool ParticleRenderer::buildPipelines()
{
    // ── Billboard (sparks, flash) — additive, depth test, no write ────────
    billboardPipeline_ = makeStoragePipeline(
        "particle_billboard.vert", "particle_billboard.frag", 1, 0, additiveBlend(), true, false, false);

    // ── Tracer capsule — additive, depth test, no write ───────────────────
    tracerPipeline_ = makeStoragePipeline("tracer.vert", "tracer.frag", 1, 0, additiveBlend(), true, false, false);

    // ── Ribbon trail — premul alpha, vertex buffer, depth test, no write ──
    {
        SDL_GPUVertexAttribute attrs[2]{};
        // location 0: vec3 pos + float pad → stride offset 0
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; // pos + _p
        attrs[0].offset = 0;
        // location 1: vec4 color
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[1].offset = 16;

        SDL_GPUVertexBufferDescription vbd{};
        vbd.slot = 0;
        vbd.pitch = sizeof(RibbonVertex);
        vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexInputState vis{};
        vis.vertex_attributes = attrs;
        vis.num_vertex_attributes = 2;
        vis.vertex_buffer_descriptions = &vbd;
        vis.num_vertex_buffers = 1;

        ribbonPipeline_ = makeVertexPipeline("ribbon.vert", "ribbon.frag", vis, premulAlphaBlend(), true, false);
    }

    // ── Hitscan beam — additive, storage ──────────────────────────────────
    hitscanPipeline_ =
        makeStoragePipeline("hitscan_beam.vert", "hitscan_beam.frag", 1, 0, additiveBlend(), true, false, false);

    // ── Lightning arc — additive, vertex buffer, triangle strip ──────────
    {
        SDL_GPUVertexAttribute attrs[2]{};
        // location 0: vec3 pos + float edge
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[0].offset = 0;
        // location 1: vec4 color
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[1].offset = 16;

        SDL_GPUVertexBufferDescription vbd{};
        vbd.slot = 0;
        vbd.pitch = sizeof(ArcVertex);
        vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexInputState vis{};
        vis.vertex_attributes = attrs;
        vis.num_vertex_attributes = 2;
        vis.vertex_buffer_descriptions = &vbd;
        vis.num_vertex_buffers = 1;

        arcPipeline_ = makeVertexPipeline("lightning_arc.vert",
                                          "lightning_arc.frag",
                                          vis,
                                          additiveBlend(),
                                          true,
                                          false,
                                          SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP);
    }

    // ── Smoke — premul alpha, storage, 1 sampler (noise) ─────────────────
    smokePipeline_ = makeStoragePipeline("smoke.vert", "smoke.frag", 1, 1, premulAlphaBlend(), true, false, false);

    // ── Decal — alpha blend, depth bias, no depth write ──────────────────
    decalPipeline_ = makeStoragePipeline("decal.vert", "decal.frag", 1, 1, alphaBlend(), true, false, true);

    // ── SDF world text — alpha blend, depth test ─────────────────────────
    sdfWorldPipeline_ = makeStoragePipeline("sdf_text.vert", "sdf_text.frag", 1, 1, alphaBlend(), true, false, false);

    // ── SDF HUD text — alpha blend, no depth ─────────────────────────────
    sdfHudPipeline_ = makeStoragePipeline("sdf_text.vert", "sdf_text.frag", 1, 1, alphaBlend(), false, false, false);

    // All pipelines are optional — missing shaders just mean that effect won't draw.
    // We only return false if a truly critical pipeline fails (none are truly critical during dev).
    return true;
}

void ParticleRenderer::quit()
{
    if (!device_)
        return;
    SDL_WaitForGPUIdle(device_);

    auto relPL = [&](SDL_GPUGraphicsPipeline*& pl) {
        if (pl) {
            SDL_ReleaseGPUGraphicsPipeline(device_, pl);
            pl = nullptr;
        }
    };
    relPL(billboardPipeline_);
    relPL(tracerPipeline_);
    relPL(ribbonPipeline_);
    relPL(hitscanPipeline_);
    relPL(arcPipeline_);
    relPL(smokePipeline_);
    relPL(decalPipeline_);
    relPL(sdfWorldPipeline_);
    relPL(sdfHudPipeline_);

    if (quadIndexBuf_) {
        SDL_ReleaseGPUBuffer(device_, quadIndexBuf_);
        quadIndexBuf_ = nullptr;
    }
    if (smokeNoise_) {
        SDL_ReleaseGPUTexture(device_, smokeNoise_);
        smokeNoise_ = nullptr;
    }
    if (decalTex_) {
        SDL_ReleaseGPUTexture(device_, decalTex_);
        decalTex_ = nullptr;
    }
    if (decalSamp_) {
        SDL_ReleaseGPUSampler(device_, decalSamp_);
        decalSamp_ = nullptr;
    }
    if (smokeSampler_) {
        SDL_ReleaseGPUSampler(device_, smokeSampler_);
        smokeSampler_ = nullptr;
    }

    billboardBuf_.quit();
    tracerBuf_.quit();
    ribbonBuf_.quit();
    hitscanBuf_.quit();
    arcBuf_.quit();
    smokeBuf_.quit();
    decalBuf_.quit();
    sdfWorldBuf_.quit();
    sdfHudBuf_.quit();

    device_ = nullptr;
}

// ---------------------------------------------------------------------------
// Upload methods
// ---------------------------------------------------------------------------

void ParticleRenderer::uploadBillboards(SDL_GPUCommandBuffer* cmd, const BillboardParticle* d, uint32_t n)
{
    billboardBuf_.upload(cmd, d, n, sizeof(BillboardParticle));
}

void ParticleRenderer::uploadTracers(SDL_GPUCommandBuffer* cmd, const TracerParticle* d, uint32_t n)
{
    tracerBuf_.upload(cmd, d, n, sizeof(TracerParticle));
}

void ParticleRenderer::uploadRibbon(SDL_GPUCommandBuffer* cmd, const RibbonVertex* d, uint32_t n)
{
    ribbonBuf_.upload(cmd, d, n, sizeof(RibbonVertex));
}

void ParticleRenderer::uploadHitscan(SDL_GPUCommandBuffer* cmd, const HitscanBeam* d, uint32_t n)
{
    hitscanBuf_.upload(cmd, d, n, sizeof(HitscanBeam));
}

void ParticleRenderer::uploadArcs(SDL_GPUCommandBuffer* cmd, const ArcVertex* d, uint32_t n)
{
    arcBuf_.upload(cmd, d, n, sizeof(ArcVertex));
}

void ParticleRenderer::uploadSmoke(SDL_GPUCommandBuffer* cmd, const SmokeParticle* d, uint32_t n)
{
    smokeBuf_.upload(cmd, d, n, sizeof(SmokeParticle));
}

void ParticleRenderer::uploadDecals(SDL_GPUCommandBuffer* cmd, const DecalInstance* d, uint32_t n)
{
    decalBuf_.upload(cmd, d, n, sizeof(DecalInstance));
}

void ParticleRenderer::uploadSdfWorld(SDL_GPUCommandBuffer* cmd, const SdfGlyphGPU* d, uint32_t n)
{
    sdfWorldBuf_.upload(cmd, d, n, sizeof(SdfGlyphGPU));
}

void ParticleRenderer::uploadSdfHud(SDL_GPUCommandBuffer* cmd, const SdfGlyphGPU* d, uint32_t n)
{
    sdfHudBuf_.upload(cmd, d, n, sizeof(SdfGlyphGPU));
}

// ---------------------------------------------------------------------------
// drawAll — ordered render pass draw calls
// ---------------------------------------------------------------------------

// std140-compatible uniform block pushed before every particle pipeline.
// Must match layout(set=1, binding=0) in all particle vertex shaders.
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

void ParticleRenderer::drawAll(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, float screenW, float screenH)
{
    // Helper lambdas
    auto bindIndex = [&]() {
        if (!quadIndexBuf_)
            return; // guard against failed buffer init
        SDL_GPUBufferBinding ib{quadIndexBuf_, 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    };
    auto drawQuads = [&](uint32_t count) {
        if (count > 0)
            SDL_DrawGPUIndexedPrimitives(pass, 6, count, 0, 0, 0);
    };

    // 1. Decals (depth bias, alpha blend)
    if (decalPipeline_ && decalBuf_.liveCount() > 0 && decalTex_ && decalSamp_) {
        SDL_BindGPUGraphicsPipeline(pass, decalPipeline_);
        bindIndex();
        decalBuf_.bindAsVertexStorage(pass, 0);
        if (decalTex_ && decalSamp_) {
            SDL_GPUTextureSamplerBinding tsb{decalTex_, decalSamp_};
            SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        }
        drawQuads(decalBuf_.liveCount());
    }

    // 2. Capsule tracers (additive)
    if (tracerPipeline_ && tracerBuf_.liveCount() > 0) {
        SDL_BindGPUGraphicsPipeline(pass, tracerPipeline_);
        bindIndex();
        tracerBuf_.bindAsVertexStorage(pass, 0);
        drawQuads(tracerBuf_.liveCount());
    }

    // 3. Ribbon trails (premul alpha, vertex buffer)
    if (ribbonPipeline_ && ribbonBuf_.liveCount() > 0) {
        SDL_BindGPUGraphicsPipeline(pass, ribbonPipeline_);
        ribbonBuf_.bindAsVertex(pass);
        SDL_DrawGPUPrimitives(pass, ribbonBuf_.liveCount(), 1, 0, 0);
    }

    // 4. Hitscan beams (additive)
    if (hitscanPipeline_ && hitscanBuf_.liveCount() > 0) {
        SDL_BindGPUGraphicsPipeline(pass, hitscanPipeline_);
        bindIndex();
        hitscanBuf_.bindAsVertexStorage(pass, 0);
        drawQuads(hitscanBuf_.liveCount());
    }

    // 5. Lightning arcs (additive, triangle strip, vertex buffer)
    if (arcPipeline_ && arcBuf_.liveCount() > 0) {
        SDL_BindGPUGraphicsPipeline(pass, arcPipeline_);
        arcBuf_.bindAsVertex(pass);
        SDL_DrawGPUPrimitives(pass, arcBuf_.liveCount(), 1, 0, 0);
    }

    // 6. Billboard sparks (additive)
    if (billboardPipeline_ && billboardBuf_.liveCount() > 0) {
        SDL_BindGPUGraphicsPipeline(pass, billboardPipeline_);
        bindIndex();
        billboardBuf_.bindAsVertexStorage(pass, 0);
        drawQuads(billboardBuf_.liveCount());
    }

    // 7. Smoke (premul alpha, noise sampler)
    if (smokePipeline_ && smokeBuf_.liveCount() > 0 && smokeNoise_ && smokeSampler_) {
        SDL_BindGPUGraphicsPipeline(pass, smokePipeline_);
        bindIndex();
        smokeBuf_.bindAsVertexStorage(pass, 0);
        SDL_GPUTextureSamplerBinding tsb{smokeNoise_, smokeSampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        drawQuads(smokeBuf_.liveCount());
    }

    // 8. World SDF text (alpha, depth test) — only draw if atlas is registered
    if (sdfWorldPipeline_ && sdfWorldBuf_.liveCount() > 0 && sdfAtlasTex_ && sdfAtlasSamp_) {
        SDL_BindGPUGraphicsPipeline(pass, sdfWorldPipeline_);
        bindIndex();
        sdfWorldBuf_.bindAsVertexStorage(pass, 0);
        SDL_GPUTextureSamplerBinding tsb{sdfAtlasTex_, sdfAtlasSamp_};
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        drawQuads(sdfWorldBuf_.liveCount());
    }

    // 9. HUD SDF text (alpha, no depth, orthographic)
    // Push a ParticleUniforms-shaped block with an ortho projection so the
    // vertex shader can use the same proj*view*vec4(pixelPos, 1.0) path
    // with pixel coordinates in worldPos/right/up.
    if (sdfHudPipeline_ && sdfHudBuf_.liveCount() > 0 && sdfAtlasTex_ && sdfAtlasSamp_) {
        const float sw = (screenW > 0) ? screenW : 1280.f;
        const float sh = (screenH > 0) ? screenH : 720.f;

        // ortho(left, right, bottom, top): pixel (0,0)=top-left → NDC (+y=up convention)
        ParticleUniforms hpu{};
        hpu.view = glm::mat4(1.0f);
        hpu.proj = glm::ortho(0.f, sw, sh, 0.f, -1.f, 1.f);
        hpu.camPos = {};
        hpu._p0 = 0;
        hpu.camRight = {1.f, 0.f, 0.f};
        hpu._p1 = 0;
        hpu.camUp = {0.f, 1.f, 0.f};
        hpu._p2 = 0;
        SDL_PushGPUVertexUniformData(cmd, 0, &hpu, sizeof(hpu));

        SDL_BindGPUGraphicsPipeline(pass, sdfHudPipeline_);
        bindIndex();
        sdfHudBuf_.bindAsVertexStorage(pass, 0);
        SDL_GPUTextureSamplerBinding tsb{sdfAtlasTex_, sdfAtlasSamp_};
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        drawQuads(sdfHudBuf_.liveCount());
    }
}
