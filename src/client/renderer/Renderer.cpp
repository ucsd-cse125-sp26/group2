#include "Renderer.hpp"

#include "Camera.hpp"
#include "ModelLoader.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <imgui.h>
#include <vector>

// stb_image_write — declaration only (implementation is in FrameRecorder.cpp).
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <stb_image_write.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace
{

// ── UBO structures (must match shader layouts exactly) ──────────────────────

/// Vertex UBO (set = 1, binding = 0) — shared by scene + PBR pipelines.
struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 normalMatrix; ///< transpose(inverse(model)), padded to mat4 for std140.
};

/// Vertex UBO for the old scene pipeline (no normalMatrix).
struct SceneMatrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

/// Fragment UBO slot 0 — per-mesh PBR material.
struct MaterialUBO
{
    glm::vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float aoStrength;
    float normalScale;
    glm::vec4 emissiveFactor;
};

/// One light in the LightData UBO.
struct LightGPU
{
    glm::vec4 position; // xyz = direction/position, w = type (0 dir, 1 point)
    glm::vec4 color;    // rgb = colour, a = intensity
    glm::vec4 params;   // x = range, y = innerCone, z = outerCone, w = castsShadow
};

/// Fragment UBO slot 1 — scene lighting.
struct LightDataUBO
{
    glm::vec4 cameraPos;
    glm::vec4 ambientColor;
    int numLights;
    float _pad1, _pad2, _pad3;
    LightGPU lights[8];
};

/// Skybox vertex UBO.
struct SkyboxMatricesUBO
{
    glm::mat4 viewRotation;
    glm::mat4 projection;
};

/// Tonemap fragment UBO.
struct TonemapParamsUBO
{
    float exposure;
    float gamma;
    int tonemapMode;
    float _pad;
};

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Shader loading helper
// ═══════════════════════════════════════════════════════════════════════════

SDL_GPUShader* Renderer::loadShaderFromFile(const char* name,
                                            SDL_GPUShaderStage stage,
                                            Uint32 samplerCount,
                                            Uint32 uniformBufferCount,
                                            Uint32 storageBufferCount,
                                            Uint32 storageTextureCount)
{
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (shaderFormat == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char path[512];
    SDL_snprintf(path, sizeof(path), "%sshaders/%s%s", k_base ? k_base : "", name, k_ext);

    size_t codeSize = 0;
    void* code = SDL_LoadFile(path, &codeSize);
    if (!code) {
        SDL_Log("Renderer: failed to load shader %s: %s", path, SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = static_cast<Uint32>(codeSize);
    info.format = shaderFormat;
    info.stage = stage;
    info.num_samplers = samplerCount;
    info.num_uniform_buffers = uniformBufferCount;
    info.num_storage_buffers = storageBufferCount;
    info.num_storage_textures = storageTextureCount;
    info.entrypoint = (shaderFormat == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(code);

    if (!shader)
        SDL_Log("Renderer: SDL_CreateGPUShader(%s) failed: %s", name, SDL_GetError());
    return shader;
}

// ═══════════════════════════════════════════════════════════════════════════
// Pipeline creation
// ═══════════════════════════════════════════════════════════════════════════

bool Renderer::initScenePipeline()
{
    // Scene geometry: uses projective.vert (1 vert UBO) + normal.frag (1 frag UBO for lights).
    SDL_GPUShader* vert = loadShaderFromFile("projective.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* frag = loadShaderFromFile("normal.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription ct{};
    ct.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; // render to HDR

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    // Y-flip in projection reverses screen-space winding; tell the GPU that CW = front.
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;

    scenePipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!scenePipeline) {
        SDL_Log("Renderer: scene pipeline creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool Renderer::initPBRPipeline()
{
    // pbr.vert: 0 samplers, 1 UBO (Matrices).
    // pbr.frag: 7 samplers (albedo, MR, emissive, normal, irradiance, prefilter, brdfLUT),
    //           2 UBOs (Material, LightData).
    SDL_GPUShader* vert = loadShaderFromFile("pbr.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* frag = loadShaderFromFile("pbr.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    // Vertex layout: position(vec3) + normal(vec3) + texCoord(vec2) + tangent(vec4) = 48 bytes.
    const SDL_GPUVertexBufferDescription vbDesc = {
        .slot = 0,
        .pitch = sizeof(ModelVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    const SDL_GPUVertexAttribute attrs[4] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12},
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24},
        {.location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 32},
    };

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = attrs;
    vertexInput.num_vertex_attributes = 4;

    // No alpha blending on the opaque PBR pipeline.
    // Transparency will be handled by a SEPARATE transparent pipeline that
    // renders after opaques with alpha blending + no depth write.
    SDL_GPUColorTargetDescription ct{};
    ct.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.vertex_input_state = vertexInput;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // GLB double-sided
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;

    pbrPipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!pbrPipeline) {
        SDL_Log("Renderer: PBR pipeline creation failed: %s", SDL_GetError());
        return false;
    }

    // ── Transparent PBR pipeline (same shaders, alpha blend, no depth write) ─
    {
        SDL_GPUColorTargetDescription ctBlend{};
        ctBlend.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ctBlend.blend_state.enable_blend = true;
        ctBlend.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ctBlend.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ctBlend.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ctBlend.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ctBlend.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ctBlend.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        // Reload shaders for the second pipeline (SDL requires separate shader objects).
        SDL_GPUShader* vertT = loadShaderFromFile("pbr.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        SDL_GPUShader* fragT = loadShaderFromFile("pbr.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2);

        SDL_GPUGraphicsPipelineCreateInfo pciT = pci; // copy from opaque
        pciT.vertex_shader = vertT;
        pciT.fragment_shader = fragT;
        pciT.target_info.color_target_descriptions = &ctBlend;
        // Read depth (test) but don't write — transparent surfaces don't occlude.
        pciT.depth_stencil_state.enable_depth_write = false;

        pbrTransparentPipeline = SDL_CreateGPUGraphicsPipeline(device, &pciT);
        SDL_ReleaseGPUShader(device, vertT);
        SDL_ReleaseGPUShader(device, fragT);

        if (!pbrTransparentPipeline)
            SDL_Log("Renderer: transparent PBR pipeline creation failed: %s", SDL_GetError());
    }

    return true;
}

bool Renderer::initSkyboxPipeline()
{
    SDL_GPUShader* vert = loadShaderFromFile("skybox.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* frag = loadShaderFromFile("skybox.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription ct{};
    ct.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    // Skybox renders at depth = 1.0; use LESS_OR_EQUAL so it fills where nothing was drawn.
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = false;     // don't overwrite depth
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // inside of the cube

    skyboxPipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!skyboxPipeline) {
        SDL_Log("Renderer: skybox pipeline creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool Renderer::initTonemapPipeline()
{
    SDL_GPUShader* vert = loadShaderFromFile("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader* frag = loadShaderFromFile("tonemap.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription ct{};
    ct.format = swapchainFormat; // output to LDR swapchain

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    // No depth test for fullscreen pass.

    tonemapPipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!tonemapPipeline) {
        SDL_Log("Renderer: tonemap pipeline creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool Renderer::initShadowPipeline()
{
    // Shadow pass: depth-only.  SDL3 GPU requires a fragment shader even when
    // only writing depth, so we use a minimal no-op shadow.frag.
    SDL_GPUShader* vert = loadShaderFromFile("shadow.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* frag = loadShaderFromFile("shadow.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    // Minimal vertex input: position only (first 12 bytes of ModelVertex).
    const SDL_GPUVertexBufferDescription vbDesc = {
        .slot = 0,
        .pitch = sizeof(ModelVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    const SDL_GPUVertexAttribute attr = {
        .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0};

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = &attr;
    vertexInput.num_vertex_attributes = 1;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.vertex_input_state = vertexInput;
    // No colour targets — depth-only pass.
    pci.target_info.num_color_targets = 0;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    // Depth bias to reduce shadow acne.
    pci.rasterizer_state.depth_bias_constant_factor = 1.25f;
    pci.rasterizer_state.depth_bias_slope_factor = 1.75f;
    pci.rasterizer_state.enable_depth_bias = true;

    shadowPipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!shadowPipeline) {
        SDL_Log("Renderer: shadow pipeline creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Texture upload
// ═══════════════════════════════════════════════════════════════════════════

SDL_GPUTexture* Renderer::uploadTexture(const uint8_t* pixels, const int width, const int height, bool sRGB)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    // Color textures (albedo, emissive) are sRGB-encoded; data textures
    // (normal, metallic-roughness) are linear.  Using the _SRGB format lets
    // the GPU hardware convert sRGB → linear on sampling automatically.
    info.format = sRGB ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &info);
    if (!tex) {
        SDL_Log("Renderer: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return nullptr;
    }

    const Uint32 dataSize = static_cast<Uint32>(width) * static_cast<Uint32>(height) * 4u;

    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = dataSize;

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb) {
        SDL_ReleaseGPUTexture(device, tex);
        return nullptr;
    }

    void* ptr = SDL_MapGPUTransferBuffer(device, tb, false);
    SDL_memcpy(ptr, pixels, dataSize);
    SDL_UnmapGPUTransferBuffer(device, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;

    SDL_GPUTextureRegion dst{};
    dst.texture = tex;
    dst.w = static_cast<Uint32>(width);
    dst.h = static_cast<Uint32>(height);
    dst.d = 1;

    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUTransferBuffer(device, tb);

    return tex;
}

// ═══════════════════════════════════════════════════════════════════════════
// Model upload
// ═══════════════════════════════════════════════════════════════════════════

bool Renderer::uploadModel(const LoadedModel& model, ModelInstance& outInstance)
{
    if (model.meshes.empty())
        return false;

    // ── Sampler (created once, shared across all models) ────────────────────
    SDL_GPUSamplerCreateInfo sampInfo{};
    sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampInfo.min_lod = 0.0f;
    sampInfo.max_lod = 1.0f;
    sampInfo.enable_anisotropy = true;
    sampInfo.max_anisotropy = 8.0f;
    sampInfo.enable_compare = false;

    pbrSampler = SDL_CreateGPUSampler(device, &sampInfo);
    if (!pbrSampler)
        return false;

    // ── Fallback textures ───────────────────────────────────────────────────
    const uint8_t white[4] = {255, 255, 255, 255};
    const uint8_t flatNormal[4] = {128, 128, 255, 255}; // (0.5, 0.5, 1.0) tangent-space up
    const uint8_t defaultMR[4] = {0, 128, 0, 255};      // metallic=0 (B), roughness=0.5 (G) — dielectric default
    const uint8_t black[4] = {0, 0, 0, 255};

    fallbackWhite = uploadTexture(white, 1, 1, true);            // sRGB color
    fallbackFlatNormal = uploadTexture(flatNormal, 1, 1, false); // linear data
    fallbackMR = uploadTexture(defaultMR, 1, 1, false);          // linear data
    fallbackBlack = uploadTexture(black, 1, 1, true);            // sRGB color

    if (!fallbackWhite || !fallbackFlatNormal || !fallbackMR || !fallbackBlack)
        return false;

    // ── Upload textures ─────────────────────────────────────────────────────
    outInstance.textures.reserve(model.textures.size());
    for (const auto& td : model.textures) {
        SDL_GPUTexture* gpuTex = uploadTexture(td.pixels.data(), td.width, td.height, td.isSRGB);
        outInstance.textures.push_back(gpuTex);
    }

    // ── Upload geometry ─────────────────────────────────────────────────────
    struct MeshSizes
    {
        Uint32 vbBytes;
        Uint32 ibBytes;
    };
    std::vector<MeshSizes> sizes;
    sizes.reserve(model.meshes.size());
    Uint32 totalBytes = 0;

    for (const auto& m : model.meshes) {
        const Uint32 vb = static_cast<Uint32>(m.vertices.size() * sizeof(ModelVertex));
        const Uint32 ib = static_cast<Uint32>(m.indices.size() * sizeof(uint32_t));
        sizes.push_back({vb, ib});
        totalBytes += vb + ib;
    }

    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = totalBytes;

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb)
        return false;

    auto* dst = static_cast<char*>(SDL_MapGPUTransferBuffer(device, tb, false));
    Uint32 writeOffset = 0;
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        SDL_memcpy(dst + writeOffset, model.meshes[i].vertices.data(), sizes[i].vbBytes);
        writeOffset += sizes[i].vbBytes;
        SDL_memcpy(dst + writeOffset, model.meshes[i].indices.data(), sizes[i].ibBytes);
        writeOffset += sizes[i].ibBytes;
    }
    SDL_UnmapGPUTransferBuffer(device, tb);

    SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    Uint32 readOffset = 0;
    outInstance.meshes.reserve(model.meshes.size());

    for (size_t i = 0; i < model.meshes.size(); ++i) {
        SDL_GPUBufferCreateInfo vbInfo{};
        vbInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vbInfo.size = sizes[i].vbBytes;
        SDL_GPUBuffer* vb = SDL_CreateGPUBuffer(device, &vbInfo);

        SDL_GPUBufferCreateInfo ibInfo{};
        ibInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        ibInfo.size = sizes[i].ibBytes;
        SDL_GPUBuffer* ib = SDL_CreateGPUBuffer(device, &ibInfo);

        if (!vb || !ib) {
            SDL_ReleaseGPUBuffer(device, vb);
            SDL_ReleaseGPUBuffer(device, ib);
            SDL_EndGPUCopyPass(copyPass);
            SDL_SubmitGPUCommandBuffer(uploadCmd);
            SDL_ReleaseGPUTransferBuffer(device, tb);
            return false;
        }

        SDL_GPUTransferBufferLocation src{};
        SDL_GPUBufferRegion dstReg{};

        src.transfer_buffer = tb;
        src.offset = readOffset;
        dstReg.buffer = vb;
        dstReg.size = sizes[i].vbBytes;
        SDL_UploadToGPUBuffer(copyPass, &src, &dstReg, false);
        readOffset += sizes[i].vbBytes;

        src.offset = readOffset;
        dstReg.buffer = ib;
        dstReg.size = sizes[i].ibBytes;
        SDL_UploadToGPUBuffer(copyPass, &src, &dstReg, false);
        readOffset += sizes[i].ibBytes;

        const auto& md = model.meshes[i];
        outInstance.meshes.push_back({
            .vertexBuffer = vb,
            .indexBuffer = ib,
            .indexCount = static_cast<Uint32>(md.indices.size()),
            .albedoTexIndex = md.diffuseTexIndex,
            .normalTexIndex = md.normalTexIndex,
            .metallicRoughnessTexIndex = md.metallicRoughnessTexIndex,
            .emissiveTexIndex = md.emissiveTexIndex,
            .material = md.material,
            .isTransparent = (md.material.alphaMode != AlphaMode::Opaque),
        });
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUTransferBuffer(device, tb);

    SDL_Log("Renderer: uploaded %zu mesh(es), %zu texture(s) (%u bytes geometry)",
            outInstance.meshes.size(),
            outInstance.textures.size(),
            totalBytes);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// IBL — generate BRDF LUT, irradiance map, and pre-filtered specular map
// ═══════════════════════════════════════════════════════════════════════════

bool Renderer::initIBL()
{
    // ── BRDF LUT (512×512 RG16F) ────────────────────────────────────────────
    {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
        ci.width = 512;
        ci.height = 512;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        brdfLUT = SDL_CreateGPUTexture(device, &ci);
        if (!brdfLUT) {
            SDL_Log("IBL: failed to create BRDF LUT: %s", SDL_GetError());
            return false;
        }

        SDL_GPUShader* cs = loadShaderFromFile("brdf_lut.comp", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 1);
        // Note: SDL3 GPU uses VERTEX stage enum for compute shaders in some versions.
        // If that fails, the actual compute pipeline creation below will catch it.
        if (!cs) {
            SDL_Log("IBL: brdf_lut.comp shader load failed");
            return false;
        }

        SDL_GPUComputePipelineCreateInfo cpci{};
        cpci.code = nullptr; // already compiled via SDL_GPUShader
        // Actually, SDL3 GPU compute pipelines are created differently.
        // Let me use the correct API.
        SDL_ReleaseGPUShader(device, cs);
    }

    // For now, generate IBL textures using a simpler approach:
    // render-to-cubemap with the existing skybox shader, then convolve.
    // However, SDL3 GPU compute pipeline creation requires specific setup.
    // Let me check the correct API pattern first.

    // TEMPORARY: Create the IBL textures with solid fallback data so the
    // shader has valid textures to sample while we implement the compute
    // pipeline properly.

    // ── Irradiance map (32×32 per face, cubemap, RGBA16F) ───────────────────
    {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_CUBE;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.width = 32;
        ci.height = 32;
        ci.layer_count_or_depth = 6;
        ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        irradianceMap = SDL_CreateGPUTexture(device, &ci);
        if (!irradianceMap) {
            SDL_Log("IBL: failed to create irradiance map");
            return false;
        }
    }

    // ── Pre-filter map (128×128 per face, 5 mip levels, cubemap, RGBA16F) ───
    {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_CUBE;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.width = 128;
        ci.height = 128;
        ci.layer_count_or_depth = 6;
        ci.num_levels = 5; // mip 0=128, 1=64, 2=32, 3=16, 4=8
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        prefilterMap = SDL_CreateGPUTexture(device, &ci);
        if (!prefilterMap) {
            SDL_Log("IBL: failed to create prefilter map");
            return false;
        }
    }

    // ── IBL sampler (linear, clamp, mipmapped) ──────────────────────────────
    {
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = SDL_GPU_FILTER_LINEAR;
        si.mag_filter = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.max_lod = 5.0f;
        iblSampler = SDL_CreateGPUSampler(device, &si);
        if (!iblSampler) {
            SDL_Log("IBL: failed to create sampler");
            return false;
        }
    }

    // TODO: Run compute shaders to fill brdfLUT, irradianceMap, prefilterMap
    // with proper IBL data.  For now they're uninitialized (black) which means
    // IBL contributes zero = same as the old simple ambient.  The compute
    // pipeline integration is complex and will be done in a follow-up.
    // For now, fill the BRDF LUT with a reasonable approximation via CPU upload.

    // ── CPU-side BRDF LUT approximation ─────────────────────────────────────
    // Use Karis's analytical fit: scale ≈ 1, bias ≈ 0 gives F0*1+0 = F0
    // which is the correct limit for a rough surface.  This is a crude
    // approximation but better than zero.
    {
        // Hammersley low-discrepancy sequence for Monte Carlo integration.
        auto radicalInverseVdC = [](uint32_t bits) -> float {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return static_cast<float>(bits) * 2.3283064365386963e-10f;
        };

        const int sz = 512;
        std::vector<uint16_t> lutData(static_cast<size_t>(sz * sz * 2));
        for (int y = 0; y < sz; ++y) {
            for (int x = 0; x < sz; ++x) {
                float NdotV = std::max((static_cast<float>(x) + 0.5f) / static_cast<float>(sz), 0.001f);
                float rough = std::max((static_cast<float>(y) + 0.5f) / static_cast<float>(sz), 0.001f);

                // Proper Monte Carlo integration of the split-sum BRDF.
                glm::vec3 V(std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
                float A = 0.0f, B = 0.0f;
                constexpr int NUM_SAMPLES = 256;
                for (int i = 0; i < NUM_SAMPLES; ++i) {
                    float xi1 = static_cast<float>(i) / static_cast<float>(NUM_SAMPLES);
                    float xi2 = radicalInverseVdC(static_cast<uint32_t>(i));

                    // Importance-sample GGX.
                    float a2 = rough * rough * rough * rough; // a = rough², a² = rough⁴
                    float phi = 6.28318f * xi1;
                    float cosTheta = std::sqrt((1.0f - xi2) / (1.0f + (a2 - 1.0f) * xi2));
                    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
                    glm::vec3 H(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
                    glm::vec3 L = 2.0f * glm::dot(V, H) * H - V;

                    float NdotL = std::max(L.z, 0.0f);
                    float NdotH = std::max(H.z, 0.0f);
                    float VdotH = std::max(glm::dot(V, H), 0.0f);

                    if (NdotL > 0.0f) {
                        // Smith-GGX geometry with IBL remapping: k = rough²/2.
                        float k = (rough * rough) / 2.0f;
                        float G1V = NdotV / (NdotV * (1.0f - k) + k);
                        float G1L = NdotL / (NdotL * (1.0f - k) + k);
                        float G = G1V * G1L;
                        float GVis = (G * VdotH) / (NdotH * NdotV + 0.0001f);
                        float Fc = std::pow(1.0f - VdotH, 5.0f);
                        A += (1.0f - Fc) * GVis;
                        B += Fc * GVis;
                    }
                }
                float scale = A / static_cast<float>(NUM_SAMPLES);
                float bias = B / static_cast<float>(NUM_SAMPLES);

                size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(sz) + static_cast<size_t>(x)) * 2;
                // Convert float to float16 (half). Use a simple truncation.
                auto toHalf = [](float v) -> uint16_t {
                    // Quick float→half conversion (loses precision but works for [0,1]).
                    uint32_t f = *reinterpret_cast<uint32_t*>(&v);
                    uint32_t sign = (f >> 16) & 0x8000;
                    int32_t exp = ((f >> 23) & 0xFF) - 127 + 15;
                    uint32_t mant = (f >> 13) & 0x03FF;
                    if (exp <= 0)
                        return static_cast<uint16_t>(sign);
                    if (exp >= 31)
                        return static_cast<uint16_t>(sign | 0x7C00);
                    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
                };

                lutData[idx + 0] = toHalf(glm::clamp(scale, 0.0f, 1.0f));
                lutData[idx + 1] = toHalf(glm::clamp(bias, 0.0f, 1.0f));
            }
        }

        // Upload to GPU.
        const Uint32 dataSize = static_cast<Uint32>(lutData.size() * sizeof(uint16_t));
        SDL_GPUTransferBufferCreateInfo tbInfo{};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = dataSize;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (tb) {
            void* ptr = SDL_MapGPUTransferBuffer(device, tb, false);
            SDL_memcpy(ptr, lutData.data(), dataSize);
            SDL_UnmapGPUTransferBuffer(device, tb);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb;
            SDL_GPUTextureRegion dst{};
            dst.texture = brdfLUT;
            dst.w = 512;
            dst.h = 512;
            dst.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, false);
            SDL_EndGPUCopyPass(cp);
            SDL_SubmitGPUCommandBuffer(cmd);
            SDL_WaitForGPUIdle(device);
            SDL_ReleaseGPUTransferBuffer(device, tb);
        }
    }

    // ── CPU-side irradiance map approximation ───────────────────────────────
    // Sample the procedural sky at low resolution for each cubemap face.
    {
        const int sz = 32;
        const size_t faceBytes = static_cast<size_t>(sz * sz * 4 * sizeof(uint16_t)); // RGBA16F
        std::vector<uint16_t> faceData(static_cast<size_t>(sz * sz * 4));

        auto toHalf = [](float v) -> uint16_t {
            uint32_t f = *reinterpret_cast<uint32_t*>(&v);
            uint32_t sign = (f >> 16) & 0x8000;
            int32_t exp = ((f >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = (f >> 13) & 0x03FF;
            if (exp <= 0)
                return static_cast<uint16_t>(sign);
            if (exp >= 31)
                return static_cast<uint16_t>(sign | 0x7C00);
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
        };

        // Procedural sky evaluation (matches skybox.frag).
        auto sky = [](glm::vec3 dir) -> glm::vec3 {
            float y = dir.y;
            // Sky values are multiplied 4× vs the skybox shader to approximate
            // the brightness of a real outdoor environment for IBL.
            glm::vec3 zenith(0.32f, 0.64f, 1.8f);
            glm::vec3 horizon(2.4f, 1.8f, 1.4f);
            glm::vec3 nadir(0.12f, 0.12f, 0.2f);
            glm::vec3 c;
            if (y > 0.0f) {
                float t = std::pow(y, 0.4f);
                c = glm::mix(horizon, zenith, t);
            } else {
                float t = std::pow(-y, 0.6f);
                c = glm::mix(horizon, nadir, t);
            }
            // Sun (simplified — no disc, just glow for irradiance).
            glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f));
            float sunGlow = std::pow(std::max(glm::dot(dir, sunDir), 0.0f), 64.0f);
            c += glm::vec3(1.0f, 0.9f, 0.7f) * sunGlow * 0.5f;
            return c;
        };

        auto cubeDir = [](int face, float u, float v) -> glm::vec3 {
            switch (face) {
            case 0:
                return glm::normalize(glm::vec3(1, -v, -u));  // +X
            case 1:
                return glm::normalize(glm::vec3(-1, -v, u));  // -X
            case 2:
                return glm::normalize(glm::vec3(u, 1, v));    // +Y
            case 3:
                return glm::normalize(glm::vec3(u, -1, -v));  // -Y
            case 4:
                return glm::normalize(glm::vec3(u, -v, 1));   // +Z
            case 5:
                return glm::normalize(glm::vec3(-u, -v, -1)); // -Z
            }
            return glm::vec3(0);
        };

        SDL_GPUTransferBufferCreateInfo tbInfo{};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = static_cast<Uint32>(faceBytes);
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (!tb)
            return false;

        for (int face = 0; face < 6; ++face) {
            for (int y = 0; y < sz; ++y) {
                for (int x = 0; x < sz; ++x) {
                    float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(sz) * 2.0f - 1.0f;
                    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(sz) * 2.0f - 1.0f;
                    glm::vec3 N = cubeDir(face, u, v);

                    // Simple hemisphere integration (low sample count for speed).
                    glm::vec3 up = std::abs(N.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    glm::vec3 right = glm::normalize(glm::cross(up, N));
                    up = glm::cross(N, right);

                    glm::vec3 irr(0.0f);
                    int samples = 0;
                    for (float phi = 0.0f; phi < 6.2832f; phi += 0.1f) {
                        for (float theta = 0.0f; theta < 1.5708f; theta += 0.1f) {
                            glm::vec3 samp = std::sin(theta) * std::cos(phi) * right +
                                             std::sin(theta) * std::sin(phi) * up + std::cos(theta) * N;
                            irr += sky(samp) * std::cos(theta) * std::sin(theta);
                            ++samples;
                        }
                    }
                    irr *= 3.14159f / static_cast<float>(samples);

                    size_t idx = (static_cast<size_t>(y) * sz + x) * 4;
                    faceData[idx + 0] = toHalf(irr.r);
                    faceData[idx + 1] = toHalf(irr.g);
                    faceData[idx + 2] = toHalf(irr.b);
                    faceData[idx + 3] = toHalf(1.0f);
                }
            }

            // Upload this face.
            void* ptr = SDL_MapGPUTransferBuffer(device, tb, false);
            SDL_memcpy(ptr, faceData.data(), faceBytes);
            SDL_UnmapGPUTransferBuffer(device, tb);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb;
            SDL_GPUTextureRegion dst{};
            dst.texture = irradianceMap;
            dst.layer = static_cast<Uint32>(face);
            dst.w = static_cast<Uint32>(sz);
            dst.h = static_cast<Uint32>(sz);
            dst.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, false);
            SDL_EndGPUCopyPass(cp);
            SDL_SubmitGPUCommandBuffer(cmd);
            SDL_WaitForGPUIdle(device);
        }
        SDL_ReleaseGPUTransferBuffer(device, tb);
    }

    // ── CPU-side prefilter map (5 mip levels, roughness = mip/4) ────────────
    {
        auto toHalf = [](float v) -> uint16_t {
            uint32_t f = *reinterpret_cast<uint32_t*>(&v);
            uint32_t sign = (f >> 16) & 0x8000;
            int32_t exp = ((f >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = (f >> 13) & 0x03FF;
            if (exp <= 0)
                return static_cast<uint16_t>(sign);
            if (exp >= 31)
                return static_cast<uint16_t>(sign | 0x7C00);
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
        };

        auto sky = [](glm::vec3 dir) -> glm::vec3 {
            float y = dir.y;
            // Sky values are multiplied 4× vs the skybox shader to approximate
            // the brightness of a real outdoor environment for IBL.
            glm::vec3 zenith(0.32f, 0.64f, 1.8f);
            glm::vec3 horizon(2.4f, 1.8f, 1.4f);
            glm::vec3 nadir(0.12f, 0.12f, 0.2f);
            glm::vec3 c;
            if (y > 0.0f)
                c = glm::mix(horizon, zenith, std::pow(y, 0.4f));
            else
                c = glm::mix(horizon, nadir, std::pow(-y, 0.6f));
            glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f));
            float sa = glm::dot(dir, sunDir);
            c += glm::vec3(1.0f, 0.95f, 0.85f) * 8.0f * std::max(0.0f, std::pow(std::max(sa, 0.0f), 2048.0f));
            c += glm::vec3(1.0f, 0.8f, 0.5f) * std::pow(std::max(sa, 0.0f), 256.0f) * 0.5f;
            return c;
        };

        auto cubeDir = [](int face, float u, float v) -> glm::vec3 {
            switch (face) {
            case 0:
                return glm::normalize(glm::vec3(1, -v, -u));
            case 1:
                return glm::normalize(glm::vec3(-1, -v, u));
            case 2:
                return glm::normalize(glm::vec3(u, 1, v));
            case 3:
                return glm::normalize(glm::vec3(u, -1, -v));
            case 4:
                return glm::normalize(glm::vec3(u, -v, 1));
            case 5:
                return glm::normalize(glm::vec3(-u, -v, -1));
            }
            return glm::vec3(0);
        };

        for (int mip = 0; mip < 5; ++mip) {
            int mipSize = 128 >> mip;             // 128, 64, 32, 16, 8
            float rough = static_cast<float>(mip) / 4.0f;
            int numSamples = (mip == 0) ? 1 : 64; // mirror for mip0, blurred for higher

            const size_t faceBytes = static_cast<size_t>(mipSize * mipSize * 4) * sizeof(uint16_t);
            std::vector<uint16_t> faceData(static_cast<size_t>(mipSize * mipSize * 4));

            SDL_GPUTransferBufferCreateInfo tbInfo{};
            tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbInfo.size = static_cast<Uint32>(faceBytes);
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
            if (!tb)
                continue;

            for (int face = 0; face < 6; ++face) {
                for (int y = 0; y < mipSize; ++y) {
                    for (int x = 0; x < mipSize; ++x) {
                        float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(mipSize) * 2.0f - 1.0f;
                        float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(mipSize) * 2.0f - 1.0f;
                        glm::vec3 N = cubeDir(face, u, v);

                        glm::vec3 color(0.0f);
                        if (numSamples == 1) {
                            color = sky(N);
                        } else {
                            // Simple cone sampling (not importance-sampled, but adequate for CPU).
                            glm::vec3 up = std::abs(N.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                            glm::vec3 right = glm::normalize(glm::cross(up, N));
                            up = glm::cross(N, right);
                            float total = 0.0f;
                            for (int s = 0; s < numSamples; ++s) {
                                // Quasi-random hemisphere direction biased by roughness.
                                float xi1 = static_cast<float>(s) / static_cast<float>(numSamples);
                                float xi2 =
                                    static_cast<float>((s * 7 + 1) % numSamples) / static_cast<float>(numSamples);
                                float phi = 6.2832f * xi1;
                                float cosTheta = std::pow(1.0f - xi2, 1.0f / (1.0f + rough * rough * 100.0f));
                                float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
                                glm::vec3 H =
                                    sinTheta * std::cos(phi) * right + sinTheta * std::sin(phi) * up + cosTheta * N;
                                glm::vec3 L = glm::normalize(2.0f * glm::dot(N, H) * H - N);
                                float NdotL = std::max(glm::dot(N, L), 0.0f);
                                if (NdotL > 0.0f) {
                                    color += sky(L) * NdotL;
                                    total += NdotL;
                                }
                            }
                            if (total > 0.0f)
                                color /= total;
                        }

                        size_t idx = (static_cast<size_t>(y) * mipSize + x) * 4;
                        faceData[idx + 0] = toHalf(color.r);
                        faceData[idx + 1] = toHalf(color.g);
                        faceData[idx + 2] = toHalf(color.b);
                        faceData[idx + 3] = toHalf(1.0f);
                    }
                }

                void* ptr = SDL_MapGPUTransferBuffer(device, tb, false);
                SDL_memcpy(ptr, faceData.data(), faceBytes);
                SDL_UnmapGPUTransferBuffer(device, tb);

                SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
                SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = tb;
                SDL_GPUTextureRegion dst{};
                dst.texture = prefilterMap;
                dst.mip_level = static_cast<Uint32>(mip);
                dst.layer = static_cast<Uint32>(face);
                dst.w = static_cast<Uint32>(mipSize);
                dst.h = static_cast<Uint32>(mipSize);
                dst.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &dst, false);
                SDL_EndGPUCopyPass(cp);
                SDL_SubmitGPUCommandBuffer(cmd);
                SDL_WaitForGPUIdle(device);
            }
            SDL_ReleaseGPUTransferBuffer(device, tb);
        }
    }

    SDL_Log("IBL: generated BRDF LUT (512x512), irradiance (32x32x6), prefilter (128x128x6 + 4 mips)");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// init
// ═══════════════════════════════════════════════════════════════════════════

bool Renderer::init(SDL_Window* win)
{
    window = win;

    constexpr SDL_GPUShaderFormat k_wantedFormats = SDL_GPU_SHADERFORMAT_SPIRV
#ifdef HAVE_MSL_SHADERS
                                                    | SDL_GPU_SHADERFORMAT_MSL
#endif
        ;

    device = SDL_CreateGPUDevice(k_wantedFormats, /*debug_mode=*/false, nullptr);
    if (!device) {
        SDL_Log("Renderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Renderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device));

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("Renderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Detect shader format and cache swapchain format.
    const SDL_GPUShaderFormat k_available = SDL_GetGPUShaderFormats(device);
    shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
    if (k_available & SDL_GPU_SHADERFORMAT_SPIRV)
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
#ifdef HAVE_MSL_SHADERS
    else if (k_available & SDL_GPU_SHADERFORMAT_MSL)
        shaderFormat = SDL_GPU_SHADERFORMAT_MSL;
#endif
    if (shaderFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format");
        return false;
    }

    swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device, window);

    // ImGui GPU backend.
    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device = device;
    imguiInfo.ColorTargetFormat = swapchainFormat;
    imguiInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo))
        return false;

    // ── Create all pipelines ────────────────────────────────────────────────
    if (!initScenePipeline())
        return false;
    if (!initPBRPipeline())
        return false;
    if (!initSkyboxPipeline())
        return false;
    if (!initTonemapPipeline())
        return false;
    // Shadow pipeline is optional — don't fail init if it doesn't work.
    initShadowPipeline();

    // Generate IBL textures (BRDF LUT, irradiance map, prefilter map).
    if (!initIBL())
        SDL_Log("Renderer: IBL init failed — metallic surfaces will appear dark");

    // ── Tonemap sampler (linear, clamp-to-edge) ─────────────────────────────
    SDL_GPUSamplerCreateInfo sampInfo{};
    sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    tonemapSampler = SDL_CreateGPUSampler(device, &sampInfo);

    // ── Load model ──────────────────────────────────────────────────────────
    // ── Load scene models ──────────────────────────────────────────────────
    const char* const k_base = SDL_GetBasePath();

    // Helper to load a model and place it in the scene.
    // flipWinding=true for models exported with CW winding (DirectX/Unity/
    // many Sketchfab downloads) that appear inside-out in our CCW pipeline.
    auto loadAndPlace = [&](const char* filename, glm::vec3 pos, float scale, bool flipUVs = false) {
        char path[512];
        SDL_snprintf(path, sizeof(path), "%sassets/%s", k_base ? k_base : "", filename);

        LoadedModel loaded;
        if (!loadModel(path, loaded, flipUVs)) {
            SDL_Log("Renderer: failed to load '%s'", filename);
            return;
        }

        ModelInstance inst;
        inst.transform = glm::scale(glm::translate(glm::mat4(1.0f), pos), glm::vec3(scale));

        if (!uploadModel(loaded, inst))
            SDL_Log("Renderer: GPU upload failed for '%s'", filename);
        else
            models.push_back(std::move(inst));
    };

    loadAndPlace("Apex_Legend_Wraith.glb", glm::vec3(200.0f, 0.0f, 400.0f), 8.0f);
    // flipUVs=true: Porsche GLB uses V=0 at bottom (Sketchfab/Blender export).
    loadAndPlace("free_1975_porsche_911_930_turbo.glb", glm::vec3(-200.0f, 0.0f, 400.0f), 40.0f, true);
    loadAndPlace("metallic_pallet_factory_store.glb", glm::vec3(0.0f, 0.0f, 600.0f), 0.25f, true);
    loadAndPlace("bottle_a.glb", glm::vec3(100.0f, 0.0f, 400.0f), 20.0f);

    // Camera — overridden every frame by drawFrame().
    camera = Camera(glm::vec3{0.0f, 100.0f, 0.0f},
                    glm::vec3{0.0f, 100.0f, 1.0f},
                    glm::vec3{0.0f, 1.0f, 0.0f},
                    fovyDegrees,
                    1.0f,
                    nearPlane,
                    farPlane);

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Render target management
// ═══════════════════════════════════════════════════════════════════════════

bool Renderer::ensureDepthTexture(const Uint32 w, const Uint32 h)
{
    if (depthTexture && depthWidth == w && depthHeight == h)
        return true;

    if (depthTexture)
        SDL_ReleaseGPUTexture(device, depthTexture);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.width = w;
    info.height = h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    depthTexture = SDL_CreateGPUTexture(device, &info);
    depthWidth = w;
    depthHeight = h;
    return depthTexture != nullptr;
}

bool Renderer::ensureHDRTarget(const Uint32 w, const Uint32 h)
{
    if (hdrTarget && hdrWidth == w && hdrHeight == h)
        return true;

    if (hdrTarget)
        SDL_ReleaseGPUTexture(device, hdrTarget);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    info.width = w;
    info.height = h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    hdrTarget = SDL_CreateGPUTexture(device, &info);
    hdrWidth = w;
    hdrHeight = h;
    return hdrTarget != nullptr;
}

bool Renderer::ensureCaptureRT(const Uint32 w, const Uint32 h, const SDL_GPUTextureFormat fmt)
{
    if (captureRT && captureRTW == w && captureRTH == h && captureRTFmt == fmt)
        return true;

    if (captureRT)
        SDL_ReleaseGPUTexture(device, captureRT);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = fmt;
    info.width = w;
    info.height = h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    captureRT = SDL_CreateGPUTexture(device, &info);
    captureRTW = w;
    captureRTH = h;
    captureRTFmt = fmt;
    return captureRT != nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// drawFrame — main render loop
// ═══════════════════════════════════════════════════════════════════════════

void Renderer::drawFrame(const glm::vec3 eye, const float yaw, const float pitch)
{
    // ── Camera setup ────────────────────────────────────────────────────────
    const float cosPitch = std::cos(pitch);
    const glm::vec3 forward{std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};
    camera.setLookAt(eye, eye + forward, glm::vec3{0.0f, 1.0f, 0.0f});

    // ── Acquire GPU resources ───────────────────────────────────────────────
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
        return;

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h) || !swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (!ensureDepthTexture(w, h) || !ensureHDRTarget(w, h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    camera.setAspect((h != 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f);

    // ── Prepare ImGui ───────────────────────────────────────────────────────
    ImDrawData* const drawData = ImGui::GetDrawData();
    if (drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);

    // ════════════════════════════════════════════════════════════════════════
    // PASS 1: Main colour pass → HDR render target
    // ════════════════════════════════════════════════════════════════════════
    {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = hdrTarget;
        ct.clear_color = {.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f};
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = depthTexture;
        dt.clear_depth = 1.0f;
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_STORE; // keep for screen-space effects later
        dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);

        // ── Scene geometry (cube + floor) ───────────────────────────────────
        if (scenePipeline) {
            SceneMatrices sceneMats{};
            sceneMats.model = glm::mat4(1.0f);
            sceneMats.view = camera.getView();
            sceneMats.projection = camera.getProjection();
            SDL_PushGPUVertexUniformData(cmd, 0, &sceneMats, sizeof(sceneMats));

            SDL_BindGPUGraphicsPipeline(pass, scenePipeline);
            SDL_DrawGPUPrimitives(pass, 42, 1, 0, 0);
        }

        // ── PBR models (two-pass: opaques first, then transparents) ────────
        if (pbrSampler && !models.empty()) {
            // Light data shared by both passes.
            LightDataUBO lightData{};
            lightData.cameraPos = glm::vec4(eye, 1.0f);
            lightData.ambientColor = glm::vec4(0.03f, 0.03f, 0.04f, 1.0f);
            lightData.numLights = 2;
            lightData.lights[0].position = glm::vec4(glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f)), 0.0f);
            lightData.lights[0].color = glm::vec4(1.0f, 0.95f, 0.85f, 3.0f);
            lightData.lights[1].position = glm::vec4(glm::normalize(glm::vec3(-0.5f, 0.3f, -0.8f)), 0.0f);
            lightData.lights[1].color = glm::vec4(0.3f, 0.4f, 0.6f, 1.0f);

            // Helper: draw all meshes matching the transparency filter.
            auto drawMeshes = [&](bool wantTransparent) {
                for (const auto& model : models) {
                    Matrices modelMats{};
                    modelMats.model = model.transform;
                    modelMats.view = camera.getView();
                    modelMats.projection = camera.getProjection();
                    modelMats.normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(model.transform)));
                    SDL_PushGPUVertexUniformData(cmd, 0, &modelMats, sizeof(modelMats));

                    for (const auto& mesh : model.meshes) {
                        if (mesh.isTransparent != wantTransparent)
                            continue;

                        MaterialUBO matUBO{};
                        matUBO.baseColorFactor = mesh.material.baseColorFactor;
                        matUBO.metallicFactor = mesh.material.metallicFactor;
                        matUBO.roughnessFactor = mesh.material.roughnessFactor;
                        matUBO.aoStrength = mesh.material.aoStrength;
                        matUBO.normalScale = mesh.material.normalScale;
                        matUBO.emissiveFactor = mesh.material.emissiveFactor;
                        SDL_PushGPUFragmentUniformData(cmd, 0, &matUBO, sizeof(matUBO));

                        const SDL_GPUBufferBinding vbBind = {.buffer = mesh.vertexBuffer, .offset = 0};
                        SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);
                        const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
                        SDL_BindGPUIndexBuffer(pass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

                        auto resolveTex = [&](int idx, SDL_GPUTexture* fallback) -> SDL_GPUTexture* {
                            if (idx >= 0 && static_cast<size_t>(idx) < model.textures.size() &&
                                model.textures[static_cast<size_t>(idx)])
                                return model.textures[static_cast<size_t>(idx)];
                            return fallback;
                        };

                        const SDL_GPUTextureSamplerBinding samplers[7] = {
                            {.texture = resolveTex(mesh.albedoTexIndex, fallbackWhite), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.metallicRoughnessTexIndex, fallbackMR), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.emissiveTexIndex, fallbackBlack), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.normalTexIndex, fallbackFlatNormal), .sampler = pbrSampler},
                            {.texture = irradianceMap, .sampler = iblSampler},
                            {.texture = prefilterMap, .sampler = iblSampler},
                            {.texture = brdfLUT, .sampler = iblSampler},
                        };
                        SDL_BindGPUFragmentSamplers(pass, 0, samplers, 7);

                        SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, 1, 0, 0, 0);
                    }
                }
            };

            // Pass 1: Opaque meshes (writes depth, no blending).
            if (pbrPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, pbrPipeline);
                SDL_PushGPUFragmentUniformData(cmd, 1, &lightData, sizeof(lightData));
                drawMeshes(false);
            }

            // Pass 2: Skybox — BEFORE transparents so transparent fragments
            // blend with the sky colour, not the black clear colour.
            if (skyboxPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, skyboxPipeline);

                SkyboxMatricesUBO skyMats{};
                glm::mat4 viewRot = camera.getView();
                viewRot[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                skyMats.viewRotation = viewRot;
                skyMats.projection = camera.getProjection();
                SDL_PushGPUVertexUniformData(cmd, 0, &skyMats, sizeof(skyMats));

                SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
            }

            // Pass 3: Transparent meshes (alpha blending, no depth write).
            // Rendered after skybox so they blend with the sky background.
            if (pbrTransparentPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, pbrTransparentPipeline);
                SDL_PushGPUFragmentUniformData(cmd, 1, &lightData, sizeof(lightData));
                drawMeshes(true);
            }
        }

        SDL_EndGPURenderPass(pass);
    }

    // ════════════════════════════════════════════════════════════════════════
    // PASS 2: Tone mapping → swapchain (or captureRT for screenshots)
    // ════════════════════════════════════════════════════════════════════════
    {
        const bool capturing = !pendingCapPath.empty() && ensureCaptureRT(w, h, swapchainFormat);
        SDL_GPUTexture* const renderTarget = capturing ? captureRT : swapchain;

        SDL_GPUColorTargetInfo ct{};
        ct.texture = renderTarget;
        ct.load_op = SDL_GPU_LOADOP_DONT_CARE; // fullscreen overwrite
        ct.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);

        if (tonemapPipeline && tonemapSampler && hdrTarget) {
            SDL_BindGPUGraphicsPipeline(pass, tonemapPipeline);

            TonemapParamsUBO params{};
            params.exposure = 1.0f;
            params.gamma = 2.2f;
            params.tonemapMode = 0; // ACES
            SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));

            const SDL_GPUTextureSamplerBinding tsb = {.texture = hdrTarget, .sampler = tonemapSampler};
            SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);

            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0); // fullscreen triangle
        }

        // ── ImGui overlay (in LDR, on top of tone-mapped image) ─────────────
        if (drawData)
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, pass);

        SDL_EndGPURenderPass(pass);

        // Blit captureRT → swapchain if capturing.
        if (capturing) {
            SDL_GPUBlitInfo blitInfo{};
            blitInfo.source.texture = captureRT;
            blitInfo.source.w = w;
            blitInfo.source.h = h;
            blitInfo.destination.texture = swapchain;
            blitInfo.destination.w = w;
            blitInfo.destination.h = h;
            blitInfo.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blitInfo.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cmd, &blitInfo);
        }
    }

    SDL_SubmitGPUCommandBuffer(cmd);

    if (!pendingCapPath.empty())
        downloadAndSaveCapture(w, h);
}

// ═══════════════════════════════════════════════════════════════════════════
// Screenshot download
// ═══════════════════════════════════════════════════════════════════════════

void Renderer::downloadAndSaveCapture(const Uint32 w, const Uint32 h)
{
    if (!captureRT || pendingCapPath.empty())
        return;

    const Uint32 dataSize = w * h * 4u;

    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbInfo.size = dataSize;
    SDL_GPUTransferBuffer* dlBuf = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!dlBuf) {
        pendingCapPath.clear();
        return;
    }

    SDL_GPUCommandBuffer* dlCmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(dlCmd);

    SDL_GPUTextureRegion srcRegion{};
    srcRegion.texture = captureRT;
    srcRegion.w = w;
    srcRegion.h = h;
    srcRegion.d = 1;

    SDL_GPUTextureTransferInfo dstTransfer{};
    dstTransfer.transfer_buffer = dlBuf;

    SDL_DownloadFromGPUTexture(cp, &srcRegion, &dstTransfer);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(dlCmd);
    SDL_WaitForGPUIdle(device);

    void* mapped = SDL_MapGPUTransferBuffer(device, dlBuf, false);
    if (mapped) {
        std::vector<uint8_t> pixels(dataSize);
        SDL_memcpy(pixels.data(), mapped, dataSize);
        SDL_UnmapGPUTransferBuffer(device, dlBuf);

        const bool swapRB = (captureRTFmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
        if (swapRB) {
            for (Uint32 i = 0; i < w * h; ++i)
                std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]);
        }

        stbi_write_png(pendingCapPath.c_str(),
                       static_cast<int>(w),
                       static_cast<int>(h),
                       4,
                       pixels.data(),
                       static_cast<int>(w) * 4);
    } else {
        SDL_UnmapGPUTransferBuffer(device, dlBuf);
    }

    SDL_ReleaseGPUTransferBuffer(device, dlBuf);
    pendingCapPath.clear();
}

// ═══════════════════════════════════════════════════════════════════════════
// Misc
// ═══════════════════════════════════════════════════════════════════════════

void Renderer::requestScreenshot(const std::string& path)
{
    pendingCapPath = path;
}

bool Renderer::setVSync(const bool enabled)
{
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!enabled) {
        if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX))
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
        else
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
    if (!SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        SDL_Log("Renderer: setVSync failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void Renderer::quit()
{
    if (!device)
        return;

    SDL_WaitForGPUIdle(device);

    // Release render targets.
    if (captureRT)
        SDL_ReleaseGPUTexture(device, captureRT);
    if (hdrTarget)
        SDL_ReleaseGPUTexture(device, hdrTarget);
    if (depthTexture)
        SDL_ReleaseGPUTexture(device, depthTexture);
    if (shadowMap)
        SDL_ReleaseGPUTexture(device, shadowMap);

    // Release IBL resources.
    if (brdfLUT)
        SDL_ReleaseGPUTexture(device, brdfLUT);
    if (irradianceMap)
        SDL_ReleaseGPUTexture(device, irradianceMap);
    if (prefilterMap)
        SDL_ReleaseGPUTexture(device, prefilterMap);
    if (iblSampler)
        SDL_ReleaseGPUSampler(device, iblSampler);

    // Release model resources.
    for (auto& inst : models) {
        for (auto& mesh : inst.meshes) {
            SDL_ReleaseGPUBuffer(device, mesh.vertexBuffer);
            SDL_ReleaseGPUBuffer(device, mesh.indexBuffer);
        }
        for (auto* tex : inst.textures)
            if (tex)
                SDL_ReleaseGPUTexture(device, tex);
    }
    models.clear();

    // Release fallback textures.
    if (fallbackWhite)
        SDL_ReleaseGPUTexture(device, fallbackWhite);
    if (fallbackFlatNormal)
        SDL_ReleaseGPUTexture(device, fallbackFlatNormal);
    if (fallbackMR)
        SDL_ReleaseGPUTexture(device, fallbackMR);
    if (fallbackBlack)
        SDL_ReleaseGPUTexture(device, fallbackBlack);

    // Release samplers.
    if (pbrSampler)
        SDL_ReleaseGPUSampler(device, pbrSampler);
    if (shadowSampler)
        SDL_ReleaseGPUSampler(device, shadowSampler);
    if (tonemapSampler)
        SDL_ReleaseGPUSampler(device, tonemapSampler);

    // Release pipelines.
    ImGui_ImplSDLGPU3_Shutdown();
    if (scenePipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, scenePipeline);
    if (pbrPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, pbrPipeline);
    if (pbrTransparentPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, pbrTransparentPipeline);
    if (skyboxPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, skyboxPipeline);
    if (tonemapPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, tonemapPipeline);
    if (shadowPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, shadowPipeline);

    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);

    device = nullptr;
    window = nullptr;
}
