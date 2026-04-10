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
    // pbr.frag: 2 samplers (albedo, metallicRoughness), 2 UBOs (Material, LightData).
    SDL_GPUShader* vert = loadShaderFromFile("pbr.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* frag = loadShaderFromFile("pbr.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2);
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

    SDL_GPUColorTargetDescription ct{};
    ct.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; // render to HDR

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

    pbrPipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!pbrPipeline) {
        SDL_Log("Renderer: PBR pipeline creation failed: %s", SDL_GetError());
        return false;
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

SDL_GPUTexture* Renderer::uploadTexture(const uint8_t* pixels, const int width, const int height, bool /*sRGB*/)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
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

bool Renderer::uploadModel(const LoadedModel& model)
{
    if (model.meshes.empty())
        return false;

    // ── Sampler ──────────────────────────────────────────────────────────────
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
    const uint8_t defaultMR[4] = {255, 128, 0, 255};    // metallic=1, roughness=0.5
    const uint8_t black[4] = {0, 0, 0, 255};

    fallbackWhite = uploadTexture(white, 1, 1);
    fallbackFlatNormal = uploadTexture(flatNormal, 1, 1);
    fallbackMR = uploadTexture(defaultMR, 1, 1);
    fallbackBlack = uploadTexture(black, 1, 1);

    if (!fallbackWhite || !fallbackFlatNormal || !fallbackMR || !fallbackBlack)
        return false;

    // ── Upload textures ─────────────────────────────────────────────────────
    modelTextures.reserve(model.textures.size());
    for (const auto& td : model.textures) {
        SDL_GPUTexture* gpuTex = uploadTexture(td.pixels.data(), td.width, td.height);
        modelTextures.push_back(gpuTex);
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
    modelMeshes.reserve(model.meshes.size());

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

        modelMeshes.push_back({
            .vertexBuffer = vb,
            .indexBuffer = ib,
            .indexCount = static_cast<Uint32>(model.meshes[i].indices.size()),
            .albedoTexIndex = model.meshes[i].diffuseTexIndex,
            .normalTexIndex = model.meshes[i].normalTexIndex,
            .metallicRoughnessTexIndex = model.meshes[i].metallicRoughnessTexIndex,
            .material = model.meshes[i].material,
        });
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUTransferBuffer(device, tb);

    SDL_Log("Renderer: uploaded %zu mesh(es), %zu texture(s) (%u bytes geometry)",
            modelMeshes.size(),
            modelTextures.size(),
            totalBytes);
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
    const char* const k_base = SDL_GetBasePath();
    char modelPath[512];
    SDL_snprintf(modelPath, sizeof(modelPath), "%sassets/Apex_Legend_Wraith.glb", k_base ? k_base : "");

    LoadedModel loadedModel;
    if (loadModel(modelPath, loadedModel)) {
        if (!uploadModel(loadedModel))
            SDL_Log("Renderer: model GPU upload failed");
    } else {
        SDL_Log("Renderer: model load failed");
    }

    modelTransform = glm::translate(glm::mat4(1.0f), glm::vec3(200.0f, 0.0f, 400.0f));
    modelTransform = glm::scale(modelTransform, glm::vec3(8.0f));

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
        ct.clear_color = {.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f};
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

        // ── PBR model ───────────────────────────────────────────────────────
        if (pbrPipeline && !modelMeshes.empty() && pbrSampler) {
            SDL_BindGPUGraphicsPipeline(pass, pbrPipeline);

            // Vertex UBO: Matrices (with normalMatrix).
            Matrices modelMats{};
            modelMats.model = modelTransform;
            modelMats.view = camera.getView();
            modelMats.projection = camera.getProjection();
            modelMats.normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(modelTransform)));
            SDL_PushGPUVertexUniformData(cmd, 0, &modelMats, sizeof(modelMats));

            // Fragment UBO slot 1: LightData (once per frame).
            LightDataUBO lightData{};
            lightData.cameraPos = glm::vec4(eye, 1.0f);
            lightData.ambientColor = glm::vec4(0.03f, 0.03f, 0.04f, 1.0f);
            lightData.numLights = 2;

            // Primary directional light (matches skybox sun direction).
            lightData.lights[0].position = glm::vec4(glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f)), 0.0f);
            lightData.lights[0].color = glm::vec4(1.0f, 0.95f, 0.85f, 3.0f);

            // Fill light — softer, from opposite side.
            lightData.lights[1].position = glm::vec4(glm::normalize(glm::vec3(-0.5f, 0.3f, -0.8f)), 0.0f);
            lightData.lights[1].color = glm::vec4(0.3f, 0.4f, 0.6f, 1.0f);

            SDL_PushGPUFragmentUniformData(cmd, 1, &lightData, sizeof(lightData));

            for (const auto& mesh : modelMeshes) {
                // Fragment UBO slot 0: Material.
                MaterialUBO matUBO{};
                matUBO.baseColorFactor = mesh.material.baseColorFactor;
                matUBO.metallicFactor = mesh.material.metallicFactor;
                matUBO.roughnessFactor = mesh.material.roughnessFactor;
                matUBO.aoStrength = mesh.material.aoStrength;
                matUBO.normalScale = mesh.material.normalScale;
                matUBO.emissiveFactor = mesh.material.emissiveFactor;
                SDL_PushGPUFragmentUniformData(cmd, 0, &matUBO, sizeof(matUBO));

                // Bind vertex/index buffers.
                const SDL_GPUBufferBinding vbBind = {.buffer = mesh.vertexBuffer, .offset = 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);
                const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
                SDL_BindGPUIndexBuffer(pass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

                // Bind textures: albedo (slot 0), metallic-roughness (slot 1).
                auto resolveTex = [&](int idx, SDL_GPUTexture* fallback) -> SDL_GPUTexture* {
                    if (idx >= 0 && static_cast<size_t>(idx) < modelTextures.size() &&
                        modelTextures[static_cast<size_t>(idx)])
                        return modelTextures[static_cast<size_t>(idx)];
                    return fallback;
                };

                const SDL_GPUTextureSamplerBinding samplers[2] = {
                    {.texture = resolveTex(mesh.albedoTexIndex, fallbackWhite), .sampler = pbrSampler},
                    {.texture = resolveTex(mesh.metallicRoughnessTexIndex, fallbackMR), .sampler = pbrSampler},
                };
                SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);

                SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, 1, 0, 0, 0);
            }
        }

        // ── Skybox ──────────────────────────────────────────────────────────
        if (skyboxPipeline) {
            SDL_BindGPUGraphicsPipeline(pass, skyboxPipeline);

            // View matrix with translation removed (rotation only).
            SkyboxMatricesUBO skyMats{};
            glm::mat4 viewRot = camera.getView();
            viewRot[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // zero translation column
            skyMats.viewRotation = viewRot;
            skyMats.projection = camera.getProjection();
            SDL_PushGPUVertexUniformData(cmd, 0, &skyMats, sizeof(skyMats));

            SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
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

    // Release model resources.
    for (auto& mesh : modelMeshes) {
        SDL_ReleaseGPUBuffer(device, mesh.vertexBuffer);
        SDL_ReleaseGPUBuffer(device, mesh.indexBuffer);
    }
    modelMeshes.clear();

    for (auto* tex : modelTextures)
        if (tex)
            SDL_ReleaseGPUTexture(device, tex);
    modelTextures.clear();

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
