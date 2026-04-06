#include "sdl3gpu_driver.hpp"

#include <SDL3/SDL.h>

#include <Ultralight/Bitmap.h>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Vertex stride constants
// ---------------------------------------------------------------------------
static constexpr uint32_t k_stride140 = 140u; // _2f_4ub_2f_2f_28f
static constexpr uint32_t k_stride20 = 20u;   // _2f_4ub_2f

// ---------------------------------------------------------------------------
// SDL3GPUDriver — constructor / destructor
// ---------------------------------------------------------------------------
SDL3GPUDriver::SDL3GPUDriver(SDL_GPUDevice* dev) : device(dev) {}

SDL3GPUDriver::~SDL3GPUDriver()
{
    // Destroy all geometry
    for (auto& [id, geo] : geometry) {
        if (geo.vertexBuf)
            SDL_ReleaseGPUBuffer(device, geo.vertexBuf);
        if (geo.indexBuf)
            SDL_ReleaseGPUBuffer(device, geo.indexBuf);
    }
    // Destroy all textures
    for (auto& [id, tex] : textures) {
        if (tex.sampler)
            SDL_ReleaseGPUSampler(device, tex.sampler);
        if (tex.texture)
            SDL_ReleaseGPUTexture(device, tex.texture);
    }
    if (dummySampler)
        SDL_ReleaseGPUSampler(device, dummySampler);
    if (dummyTexture)
        SDL_ReleaseGPUTexture(device, dummyTexture);
    if (fillPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, fillPipeline);
    if (fillPathPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, fillPathPipeline);
    if (compositePipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, compositePipeline);
}

// ---------------------------------------------------------------------------
// buildPipelines
// ---------------------------------------------------------------------------
SDL_GPUShader*
SDL3GPUDriver::loadSPIRV(const char* path, SDL_GPUShaderStage stage, uint32_t numSamplers, uint32_t numUniformBuffers)
{
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_Log("[UL] loadSPIRV: cannot open %s: %s", path, SDL_GetError());
        return nullptr;
    }
    Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) {
        SDL_CloseIO(io);
        SDL_Log("[UL] loadSPIRV: zero-size file %s", path);
        return nullptr;
    }
    std::vector<uint8_t> code(static_cast<size_t>(sz));
    SDL_ReadIO(io, code.data(), code.size());
    SDL_CloseIO(io);

    SDL_GPUShaderCreateInfo sci{};
    sci.code = code.data();
    sci.code_size = code.size();
    sci.entrypoint = "main";
    sci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    sci.stage = stage;
    sci.num_samplers = numSamplers;
    sci.num_uniform_buffers = numUniformBuffers;
    sci.num_storage_textures = 0;
    sci.num_storage_buffers = 0;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &sci);
    if (!shader)
        SDL_Log("[UL] SDL_CreateGPUShader failed for %s: %s", path, SDL_GetError());
    return shader;
}

SDL_GPUGraphicsPipeline* SDL3GPUDriver::buildPipelineInternal(SDL_GPUShader* vert,
                                                              SDL_GPUShader* frag,
                                                              ultralight::VertexBufferFormat vfmt,
                                                              SDL_GPUTextureFormat targetFmt,
                                                              bool enableBlend)
{
    const bool k_isFill = (vfmt == ultralight::VertexBufferFormat::_2f_4ub_2f_2f_28f);
    const uint32_t k_stride = k_isFill ? k_stride140 : k_stride20;

    // --- vertex attributes ---
    SDL_GPUVertexAttribute attrs[11]{};
    uint32_t numAttrs = 0;

    if (k_isFill) {
        // _2f_4ub_2f_2f_28f: 11 attributes
        // loc 0: pos  float2  offset 0
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = 0;
        // loc 1: color ubyte4_norm offset 8
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        attrs[1].offset = 8;
        // loc 2: tex float2 offset 12
        attrs[2].location = 2;
        attrs[2].buffer_slot = 0;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[2].offset = 12;
        // loc 3: obj float2 offset 20
        attrs[3].location = 3;
        attrs[3].buffer_slot = 0;
        attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[3].offset = 20;
        // loc 4: data0 float4 offset 28
        attrs[4].location = 4;
        attrs[4].buffer_slot = 0;
        attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[4].offset = 28;
        // loc 5: data1 float4 offset 44
        attrs[5].location = 5;
        attrs[5].buffer_slot = 0;
        attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[5].offset = 44;
        // loc 6: data2 float4 offset 60
        attrs[6].location = 6;
        attrs[6].buffer_slot = 0;
        attrs[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[6].offset = 60;
        // loc 7: data3 float4 offset 76
        attrs[7].location = 7;
        attrs[7].buffer_slot = 0;
        attrs[7].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[7].offset = 76;
        // loc 8: data4 float4 offset 92
        attrs[8].location = 8;
        attrs[8].buffer_slot = 0;
        attrs[8].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[8].offset = 92;
        // loc 9: data5 float4 offset 108
        attrs[9].location = 9;
        attrs[9].buffer_slot = 0;
        attrs[9].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[9].offset = 108;
        // loc 10: data6 float4 offset 124
        attrs[10].location = 10;
        attrs[10].buffer_slot = 0;
        attrs[10].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[10].offset = 124;
        numAttrs = 11;
    } else {
        // _2f_4ub_2f: 3 attributes
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        attrs[1].offset = 8;
        attrs[2].location = 2;
        attrs[2].buffer_slot = 0;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[2].offset = 12;
        numAttrs = 3;
    }

    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = k_stride;
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbDesc.instance_step_rate = 0;

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = (vert != nullptr && frag != nullptr &&
                                      vfmt != ultralight::VertexBufferFormat::_2f_4ub_2f_2f_28f && numAttrs == 0)
                                         ? 0u
                                         : 1u;
    // For composite pipeline (no vertex buffer), set to 0
    if (numAttrs == 0) {
        vertexInput.num_vertex_buffers = 0;
    }
    vertexInput.vertex_attributes = numAttrs > 0 ? attrs : nullptr;
    vertexInput.num_vertex_attributes = numAttrs;

    SDL_GPUColorTargetBlendState blendState{};
    if (enableBlend) {
        blendState.enable_blend = true;
        blendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        blendState.color_write_mask =
            SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    } else {
        blendState.enable_blend = false;
        blendState.color_write_mask =
            SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = targetFmt;
    colorTarget.blend_state = blendState;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.vertex_input_state = vertexInput;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &colorTarget;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = false;

    SDL_GPURasterizerState rasterizer{};
    rasterizer.fill_mode = SDL_GPU_FILLMODE_FILL;
    rasterizer.cull_mode = SDL_GPU_CULLMODE_NONE;
    rasterizer.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.rasterizer_state = rasterizer;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    if (!pipeline)
        SDL_Log("[UL] SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
    return pipeline;
}

bool SDL3GPUDriver::buildPipelines(const char* basePath, SDL_Window* window)
{
    std::string base(basePath);
    // Ensure trailing slash
    if (!base.empty() && base.back() != '/')
        base += '/';
    const std::string k_shaderDir = base + "shaders/ultralight/";

    // --- dummy 1×1 white BGRA texture ---
    {
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = 1;
        ti.height = 1;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        dummyTexture = SDL_CreateGPUTexture(device, &ti);
        if (!dummyTexture) {
            SDL_Log("[UL] Failed to create dummy texture: %s", SDL_GetError());
            return false;
        }

        // Upload white pixel
        uint8_t white[4] = {255, 255, 255, 255};
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = 4;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbi);
        if (tb) {
            void* mapped = SDL_MapGPUTransferBuffer(device, tb, false);
            if (mapped) {
                memcpy(mapped, white, 4);
                SDL_UnmapGPUTransferBuffer(device, tb);
            }
            SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);
            if (uploadCmd) {
                SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(uploadCmd);
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = tb;
                src.offset = 0;
                src.pixels_per_row = 1;
                src.rows_per_layer = 1;
                SDL_GPUTextureRegion dst{};
                dst.texture = dummyTexture;
                dst.w = 1;
                dst.h = 1;
                dst.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &dst, false);
                SDL_EndGPUCopyPass(cp);
                SDL_SubmitGPUCommandBuffer(uploadCmd);
            }
            SDL_ReleaseGPUTransferBuffer(device, tb);
        }
    }

    // --- dummy sampler ---
    {
        SDL_GPUSamplerCreateInfo sci{};
        sci.min_filter = SDL_GPU_FILTER_LINEAR;
        sci.mag_filter = SDL_GPU_FILTER_LINEAR;
        sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        dummySampler = SDL_CreateGPUSampler(device, &sci);
        if (!dummySampler) {
            SDL_Log("[UL] Failed to create dummy sampler: %s", SDL_GetError());
            return false;
        }
    }

    // --- load shaders ---
    auto fillVertPath = k_shaderDir + "fill.vert.spv";
    auto fillFragPath = k_shaderDir + "fill.frag.spv";
    auto fillPathVertPath = k_shaderDir + "fill_path.vert.spv";
    auto fillPathFragPath = k_shaderDir + "fill_path.frag.spv";
    auto compositeVertPath = k_shaderDir + "composite.vert.spv";
    auto compositeFragPath = k_shaderDir + "composite.frag.spv";

    // fill.vert: 0 samplers, 1 uniform buffer (VertexUniforms at set=1)
    SDL_GPUShader* fillVert = loadSPIRV(fillVertPath.c_str(), SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    // fill.frag: 2 samplers, 1 uniform buffer (FragUniforms at set=3)
    SDL_GPUShader* fillFrag = loadSPIRV(fillFragPath.c_str(), SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
    // fill_path.vert: 0 samplers, 1 uniform buffer
    SDL_GPUShader* fillPathVert = loadSPIRV(fillPathVertPath.c_str(), SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    // fill_path.frag: 1 sampler (sPattern), 1 uniform buffer (FragUniforms)
    SDL_GPUShader* fillPathFrag = loadSPIRV(fillPathFragPath.c_str(), SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    // composite.vert: 0 samplers, 0 uniform buffers
    SDL_GPUShader* compositeVert = loadSPIRV(compositeVertPath.c_str(), SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    // composite.frag: 1 sampler, 0 uniform buffers
    SDL_GPUShader* compositeFrag = loadSPIRV(compositeFragPath.c_str(), SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

    bool ok = fillVert && fillFrag && fillPathVert && fillPathFrag && compositeVert && compositeFrag;

    if (ok) {
        // Query the real swapchain format so the composite pipeline's attachment format matches.
        SDL_GPUTextureFormat swapFmt = SDL_GetGPUSwapchainTextureFormat(device, window);
        if (swapFmt == SDL_GPU_TEXTUREFORMAT_INVALID)
            swapFmt = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        SDL_Log("[UL] swapchain format = %d", static_cast<int>(swapFmt));

        fillPipeline = buildPipelineInternal(fillVert,
                                             fillFrag,
                                             ultralight::VertexBufferFormat::_2f_4ub_2f_2f_28f,
                                             SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                             true);

        fillPathPipeline = buildPipelineInternal(fillPathVert,
                                                 fillPathFrag,
                                                 ultralight::VertexBufferFormat::_2f_4ub_2f,
                                                 SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                                 true);

        // Composite pipeline renders into the swapchain — no vertex buffer needed (full-screen tri)
        // Build a special pipeline for composite: no vertex attributes
        // We'll handle this as a special case in buildPipelineInternal by passing a dummy vfmt
        // that results in no vertex buffer. Use a non-existent vfmt value trick:
        // Actually, let's build composite manually here.
        {
            SDL_GPUColorTargetBlendState blendState{};
            blendState.enable_blend = true;
            blendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            blendState.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                          SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

            SDL_GPUColorTargetDescription colorTarget{};
            colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, nullptr);
            if (colorTarget.format == SDL_GPU_TEXTUREFORMAT_INVALID)
                colorTarget.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
            colorTarget.blend_state = blendState;

            SDL_GPUVertexInputState vertexInput{};
            vertexInput.vertex_buffer_descriptions = nullptr;
            vertexInput.num_vertex_buffers = 0;
            vertexInput.vertex_attributes = nullptr;
            vertexInput.num_vertex_attributes = 0;

            SDL_GPURasterizerState rasterizer{};
            rasterizer.fill_mode = SDL_GPU_FILLMODE_FILL;
            rasterizer.cull_mode = SDL_GPU_CULLMODE_NONE;
            rasterizer.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

            SDL_GPUGraphicsPipelineCreateInfo pci{};
            pci.vertex_shader = compositeVert;
            pci.fragment_shader = compositeFrag;
            pci.vertex_input_state = vertexInput;
            pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pci.target_info.color_target_descriptions = &colorTarget;
            pci.target_info.num_color_targets = 1;
            pci.target_info.has_depth_stencil_target = false;
            pci.rasterizer_state = rasterizer;

            compositePipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
            if (!compositePipeline)
                SDL_Log("[UL] composite pipeline failed: %s", SDL_GetError());
        }

        ok = fillPipeline && fillPathPipeline && compositePipeline;
    }

    // Release shaders (they're now owned by the pipelines)
    if (fillVert)
        SDL_ReleaseGPUShader(device, fillVert);
    if (fillFrag)
        SDL_ReleaseGPUShader(device, fillFrag);
    if (fillPathVert)
        SDL_ReleaseGPUShader(device, fillPathVert);
    if (fillPathFrag)
        SDL_ReleaseGPUShader(device, fillPathFrag);
    if (compositeVert)
        SDL_ReleaseGPUShader(device, compositeVert);
    if (compositeFrag)
        SDL_ReleaseGPUShader(device, compositeFrag);

    return ok;
}

// ---------------------------------------------------------------------------
// GPUDriver — synchronize (no-ops for our single-threaded driver)
// ---------------------------------------------------------------------------
void SDL3GPUDriver::BeginSynchronize() {}
void SDL3GPUDriver::EndSynchronize() {}

// ---------------------------------------------------------------------------
// GPUDriver — texture management
// ---------------------------------------------------------------------------
uint32_t SDL3GPUDriver::NextTextureId()
{
    return nextTexId++;
}

void SDL3GPUDriver::CreateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm)
{
    GpuTexture tex{};

    bool isRtt = !bm || bm->IsEmpty();

    if (isRtt) {
        // Render-target texture — created with COLOR_TARGET usage too
        tex.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        // Width/height come later via RenderBuffer but Ultralight calls CreateTexture first
        // with an empty bitmap of the correct dimensions if accelerated.
        // If truly empty, use 1×1 until resized.
        tex.width = bm ? bm->width() : 1u;
        tex.height = bm ? bm->height() : 1u;
        // Make sure we have non-zero dimensions
        if (tex.width == 0)
            tex.width = 1;
        if (tex.height == 0)
            tex.height = 1;

        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = tex.format;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        ti.width = tex.width;
        ti.height = tex.height;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        tex.texture = SDL_CreateGPUTexture(device, &ti);
        if (!tex.texture)
            SDL_Log("[UL] CreateTexture RTT %u (%ux%u) failed: %s", id, tex.width, tex.height, SDL_GetError());
    } else {
        tex.width = bm->width();
        tex.height = bm->height();
        tex.format = (bm->format() == ultralight::BitmapFormat::A8_UNORM) ? SDL_GPU_TEXTUREFORMAT_R8_UNORM
                                                                          : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = tex.format;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = tex.width;
        ti.height = tex.height;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        tex.texture = SDL_CreateGPUTexture(device, &ti);
        if (!tex.texture) {
            SDL_Log("[UL] CreateTexture %u (%ux%u) failed: %s", id, tex.width, tex.height, SDL_GetError());
        } else {
            // Copy pixel data for deferred upload
            auto locked = bm->LockPixelsSafe();
            if (locked) {
                size_t sz = bm->size();
                tex.pendingData.resize(sz);
                memcpy(tex.pendingData.data(), locked.data(), sz);
                tex.dirty = true;
            }
        }
    }

    // Create sampler
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    tex.sampler = SDL_CreateGPUSampler(device, &sci);

    textures[id] = std::move(tex);
}

void SDL3GPUDriver::UpdateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm)
{
    auto it = textures.find(id);
    if (it == textures.end() || !bm || bm->IsEmpty())
        return;

    auto& tex = it->second;
    auto locked = bm->LockPixelsSafe();
    if (locked) {
        size_t sz = bm->size();
        tex.pendingData.resize(sz);
        memcpy(tex.pendingData.data(), locked.data(), sz);
        tex.dirty = true;
    }
}

void SDL3GPUDriver::DestroyTexture(uint32_t id)
{
    auto it = textures.find(id);
    if (it == textures.end())
        return;
    auto& tex = it->second;
    if (tex.sampler)
        SDL_ReleaseGPUSampler(device, tex.sampler);
    if (tex.texture)
        SDL_ReleaseGPUTexture(device, tex.texture);
    textures.erase(it);
}

// ---------------------------------------------------------------------------
// GPUDriver — render buffers
// ---------------------------------------------------------------------------
uint32_t SDL3GPUDriver::NextRenderBufferId()
{
    return nextRbId++;
}

void SDL3GPUDriver::CreateRenderBuffer(uint32_t id, const ultralight::RenderBuffer& rb)
{
    GpuRenderBuffer buf{};
    buf.textureId = rb.texture_id;
    renderBuffers[id] = buf;
}

void SDL3GPUDriver::DestroyRenderBuffer(uint32_t id)
{
    renderBuffers.erase(id);
}

// ---------------------------------------------------------------------------
// GPUDriver — geometry
// ---------------------------------------------------------------------------
uint32_t SDL3GPUDriver::NextGeometryId()
{
    return nextGeoId++;
}

static void uploadGeometryBuffers(SDL_GPUDevice* dev,
                                  SDL_GPUBuffer*& vbuf,
                                  SDL_GPUBuffer*& ibuf,
                                  const ultralight::VertexBuffer& vb,
                                  const ultralight::IndexBuffer& ib)
{
    // Vertex buffer
    if (vbuf)
        SDL_ReleaseGPUBuffer(dev, vbuf);
    {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = vb.size;
        vbuf = SDL_CreateGPUBuffer(dev, &bci);
    }

    // Index buffer
    if (ibuf)
        SDL_ReleaseGPUBuffer(dev, ibuf);
    {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        bci.size = ib.size;
        ibuf = SDL_CreateGPUBuffer(dev, &bci);
    }

    uint32_t totalSize = vb.size + ib.size;

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = totalSize;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb)
        return;

    void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (mapped) {
        memcpy(mapped, vb.data, vb.size);
        memcpy(static_cast<uint8_t*>(mapped) + vb.size, ib.data, ib.size);
        SDL_UnmapGPUTransferBuffer(dev, tb);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (cmd) {
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferLocation vSrc{};
        vSrc.transfer_buffer = tb;
        vSrc.offset = 0;
        SDL_GPUBufferRegion vDst{};
        vDst.buffer = vbuf;
        vDst.offset = 0;
        vDst.size = vb.size;
        SDL_UploadToGPUBuffer(cp, &vSrc, &vDst, false);

        SDL_GPUTransferBufferLocation iSrc{};
        iSrc.transfer_buffer = tb;
        iSrc.offset = vb.size;
        SDL_GPUBufferRegion iDst{};
        iDst.buffer = ibuf;
        iDst.offset = 0;
        iDst.size = ib.size;
        SDL_UploadToGPUBuffer(cp, &iSrc, &iDst, false);

        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
    }

    SDL_ReleaseGPUTransferBuffer(dev, tb);
}

void SDL3GPUDriver::CreateGeometry(uint32_t id, const ultralight::VertexBuffer& vb, const ultralight::IndexBuffer& ib)
{
    GpuGeometry geo{};
    geo.vfmt = vb.format;
    geo.indexCount = ib.size / sizeof(uint32_t);
    uploadGeometryBuffers(device, geo.vertexBuf, geo.indexBuf, vb, ib);
    geometry[id] = geo;
}

void SDL3GPUDriver::UpdateGeometry(uint32_t id, const ultralight::VertexBuffer& vb, const ultralight::IndexBuffer& ib)
{
    auto it = geometry.find(id);
    if (it == geometry.end())
        return;
    auto& geo = it->second;
    geo.vfmt = vb.format;
    geo.indexCount = ib.size / sizeof(uint32_t);
    uploadGeometryBuffers(device, geo.vertexBuf, geo.indexBuf, vb, ib);
}

void SDL3GPUDriver::DestroyGeometry(uint32_t id)
{
    auto it = geometry.find(id);
    if (it == geometry.end())
        return;
    auto& geo = it->second;
    if (geo.vertexBuf)
        SDL_ReleaseGPUBuffer(device, geo.vertexBuf);
    if (geo.indexBuf)
        SDL_ReleaseGPUBuffer(device, geo.indexBuf);
    geometry.erase(it);
}

// ---------------------------------------------------------------------------
// GPUDriver — command list
// ---------------------------------------------------------------------------
void SDL3GPUDriver::UpdateCommandList(const ultralight::CommandList& list)
{
    pendingCommands.reserve(pendingCommands.size() + list.size);
    for (uint32_t i = 0; i < list.size; ++i)
        pendingCommands.push_back(PendingCommand{list.commands[i]});
}

// ---------------------------------------------------------------------------
// getTexture / getSampler
// ---------------------------------------------------------------------------
SDL_GPUTexture* SDL3GPUDriver::getTexture(uint32_t textureId) const
{
    if (textureId == 0)
        return dummyTexture;
    auto it = textures.find(textureId);
    return (it != textures.end()) ? it->second.texture : dummyTexture;
}

SDL_GPUSampler* SDL3GPUDriver::getSampler(uint32_t textureId) const
{
    if (textureId == 0)
        return dummySampler;
    auto it = textures.find(textureId);
    return (it != textures.end()) ? it->second.sampler : dummySampler;
}

// ---------------------------------------------------------------------------
// Uniform helpers
// ---------------------------------------------------------------------------
void SDL3GPUDriver::fillVertexUniforms(VertexUniforms& out, const ultralight::GPUState& s) const
{
    memcpy(out.transform, s.transform.data, sizeof(float) * 16);
}

void SDL3GPUDriver::fillFragUniforms(FragUniforms& out, const ultralight::GPUState& s) const
{
    out.state[0] = 0.f;
    out.state[1] = static_cast<float>(s.viewport_width);
    out.state[2] = static_cast<float>(s.viewport_height);
    out.state[3] = 0.f;

    memcpy(out.transform, s.transform.data, sizeof(float) * 16);

    for (int i = 0; i < 8; ++i)
        out.scalar4[i] = s.uniform_scalar[i];

    for (int i = 0; i < 8; ++i) {
        out.vector[i * 4 + 0] = s.uniform_vector[i].x;
        out.vector[i * 4 + 1] = s.uniform_vector[i].y;
        out.vector[i * 4 + 2] = s.uniform_vector[i].z;
        out.vector[i * 4 + 3] = s.uniform_vector[i].w;
    }

    out.clipSize = s.clip_size;
    out.pad[0] = out.pad[1] = out.pad[2] = 0;

    for (int c = 0; c < 8; ++c)
        memcpy(out.clip[c], s.clip[c].data, sizeof(float) * 16);
}

// ---------------------------------------------------------------------------
// uploadDirtyTextures
// ---------------------------------------------------------------------------
void SDL3GPUDriver::uploadDirtyTextures(SDL_GPUCommandBuffer* cmdBuf)
{
    for (auto& [id, tex] : textures) {
        if (!tex.dirty || tex.pendingData.empty())
            continue;

        uint32_t dataSize = static_cast<uint32_t>(tex.pendingData.size());

        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = dataSize;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbi);
        if (!tb)
            continue;

        void* mapped = SDL_MapGPUTransferBuffer(device, tb, false);
        if (mapped) {
            memcpy(mapped, tex.pendingData.data(), dataSize);
            SDL_UnmapGPUTransferBuffer(device, tb);
        }

        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmdBuf);

        uint32_t bpp = (tex.format == SDL_GPU_TEXTUREFORMAT_R8_UNORM) ? 1u : 4u;

        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        src.offset = 0;
        src.pixels_per_row = tex.width;
        src.rows_per_layer = tex.height;

        SDL_GPUTextureRegion dst{};
        dst.texture = tex.texture;
        dst.w = tex.width;
        dst.h = tex.height;
        dst.d = 1;

        (void)bpp; // used implicitly in pixels_per_row / rows_per_layer calculation
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);

        SDL_ReleaseGPUTransferBuffer(device, tb);

        tex.pendingData.clear();
        tex.dirty = false;
    }
}

// ---------------------------------------------------------------------------
// executeCommand
// ---------------------------------------------------------------------------
void SDL3GPUDriver::executeCommand(SDL_GPUCommandBuffer* cmdBuf, const ultralight::Command& cmd)
{
    if (cmd.command_type == ultralight::CommandType::ClearRenderBuffer) {
        auto rbIt = renderBuffers.find(cmd.gpu_state.render_buffer_id);
        if (rbIt == renderBuffers.end())
            return;
        uint32_t texId = rbIt->second.textureId;
        SDL_GPUTexture* rtt = getTexture(texId);
        if (!rtt)
            return;

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = rtt;
        colorTarget.clear_color = {.r = 0.f, .g = 0.f, .b = 0.f, .a = 0.f};
        colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, nullptr);
        if (pass)
            SDL_EndGPURenderPass(pass);
        return;
    }

    if (cmd.command_type == ultralight::CommandType::DrawGeometry) {
        auto geoIt = geometry.find(cmd.geometry_id);
        if (geoIt == geometry.end())
            return;
        auto& geo = geoIt->second;

        auto rbIt = renderBuffers.find(cmd.gpu_state.render_buffer_id);
        if (rbIt == renderBuffers.end())
            return;
        uint32_t texId = rbIt->second.textureId;
        SDL_GPUTexture* rtt = getTexture(texId);
        if (!rtt)
            return;

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = rtt;
        colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, nullptr);
        if (!pass)
            return;

        // Choose pipeline
        SDL_GPUGraphicsPipeline* pipeline =
            (geo.vfmt == ultralight::VertexBufferFormat::_2f_4ub_2f_2f_28f) ? fillPipeline : fillPathPipeline;
        SDL_BindGPUGraphicsPipeline(pass, pipeline);

        // Scissor
        if (cmd.gpu_state.enable_scissor) {
            SDL_Rect sr{};
            sr.x = cmd.gpu_state.scissor_rect.left;
            sr.y = cmd.gpu_state.scissor_rect.top;
            sr.w = cmd.gpu_state.scissor_rect.right - cmd.gpu_state.scissor_rect.left;
            sr.h = cmd.gpu_state.scissor_rect.bottom - cmd.gpu_state.scissor_rect.top;
            SDL_SetGPUScissor(pass, &sr);
        }

        // Bind textures (slots 0 and 1)
        {
            SDL_GPUTextureSamplerBinding bindings[2]{};
            bindings[0].texture = getTexture(cmd.gpu_state.texture_1_id);
            bindings[0].sampler = getSampler(cmd.gpu_state.texture_1_id);
            bindings[1].texture = getTexture(cmd.gpu_state.texture_2_id);
            bindings[1].sampler = getSampler(cmd.gpu_state.texture_2_id);
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
        }

        // Bind vertex + index buffers
        {
            SDL_GPUBufferBinding vb{};
            vb.buffer = geo.vertexBuf;
            vb.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

            SDL_GPUBufferBinding ib{};
            ib.buffer = geo.indexBuf;
            ib.offset = 0;
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        }

        // Push vertex uniforms (slot 0)
        VertexUniforms vu{};
        fillVertexUniforms(vu, cmd.gpu_state);
        SDL_PushGPUVertexUniformData(cmdBuf, 0, &vu, sizeof(vu));

        // Push fragment uniforms (slot 0)
        FragUniforms fu{};
        fillFragUniforms(fu, cmd.gpu_state);
        SDL_PushGPUFragmentUniformData(cmdBuf, 0, &fu, sizeof(fu));

        SDL_DrawGPUIndexedPrimitives(pass, cmd.indices_count, 1, cmd.indices_offset, 0, 0);

        SDL_EndGPURenderPass(pass);
        return;
    }
}

// ---------------------------------------------------------------------------
// flushCommands
// ---------------------------------------------------------------------------
void SDL3GPUDriver::flushCommands(SDL_GPUCommandBuffer* cmdBuf)
{
    uploadDirtyTextures(cmdBuf);

    // One-shot diagnostic: log command count on first non-empty flush
    if (!pendingCommands.empty() && !commandsEverFlushed) {
        SDL_Log("[UL] first non-empty flush: %zu commands", pendingCommands.size());
        commandsEverFlushed = true;
    }

    for (auto& pc : pendingCommands)
        executeCommand(cmdBuf, pc.cmd);

    pendingCommands.clear();
}
