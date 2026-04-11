#include "Renderer.hpp"

#include "Camera.hpp"
#include "ModelLoader.hpp"
#include "particles/ParticleSystem.hpp"

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

/// Shadow map vertex UBO — matches shadow.vert LightMatrices.
struct ShadowUBO
{
    glm::mat4 lightVP;
    glm::mat4 model;
};

/// Shadow data pushed to pbr.frag for shadow sampling.
struct ShadowDataFragUBO
{
    glm::mat4 lightVP; ///< Light's view-projection matrix.
    float shadowBias;
    float shadowNormalBias;
    float shadowMapSize;
    float _pad;
};

/// Tonemap fragment UBO — matches tonemap.frag TonemapParams.
struct TonemapParamsUBO
{
    float exposure;
    float gamma;
    int tonemapMode;
    float bloomStrength;
    float ssaoStrength;
    float ssrStrength;
    float volumetricStrength;
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
// Compute pipeline helper
// ═══════════════════════════════════════════════════════════════════════════

SDL_GPUComputePipeline* Renderer::createComputePipeline(const char* shaderName,
                                                        Uint32 numSamplers,
                                                        Uint32 numReadonlyStorageTextures,
                                                        Uint32 numReadonlyStorageBuffers,
                                                        Uint32 numReadwriteStorageTextures,
                                                        Uint32 numReadwriteStorageBuffers,
                                                        Uint32 numUniformBuffers,
                                                        Uint32 threadCountX,
                                                        Uint32 threadCountY,
                                                        Uint32 threadCountZ)
{
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (shaderFormat == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char path[512];
    SDL_snprintf(path, sizeof(path), "%sshaders/%s%s", k_base ? k_base : "", shaderName, k_ext);

    size_t codeSize = 0;
    void* code = SDL_LoadFile(path, &codeSize);
    if (!code) {
        SDL_Log("Renderer: failed to load compute shader %s: %s", path, SDL_GetError());
        return nullptr;
    }

    SDL_GPUComputePipelineCreateInfo cpci{};
    cpci.code_size = codeSize;
    cpci.code = static_cast<const Uint8*>(code);
    cpci.entrypoint = (shaderFormat == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";
    cpci.format = shaderFormat;
    cpci.num_samplers = numSamplers;
    cpci.num_readonly_storage_textures = numReadonlyStorageTextures;
    cpci.num_readonly_storage_buffers = numReadonlyStorageBuffers;
    cpci.num_readwrite_storage_textures = numReadwriteStorageTextures;
    cpci.num_readwrite_storage_buffers = numReadwriteStorageBuffers;
    cpci.num_uniform_buffers = numUniformBuffers;
    cpci.threadcount_x = threadCountX;
    cpci.threadcount_y = threadCountY;
    cpci.threadcount_z = threadCountZ;

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(device, &cpci);
    SDL_free(code);

    if (!pipeline)
        SDL_Log("Renderer: compute pipeline '%s' creation failed: %s", shaderName, SDL_GetError());
    return pipeline;
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
    SDL_GPUShader* frag = loadShaderFromFile("pbr.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 8, 3);
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
    SDL_GPUShader* frag = loadShaderFromFile("tonemap.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 5, 1);
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
    // Compute full mip chain levels: floor(log2(max(w,h))) + 1.
    const Uint32 w = static_cast<Uint32>(width);
    const Uint32 h = static_cast<Uint32>(height);
    const Uint32 maxDim = std::max(w, h);
    Uint32 numLevels = 1;
    {
        Uint32 dim = maxDim;
        while (dim > 1) {
            dim >>= 1;
            ++numLevels;
        }
    }
    // For tiny textures (1×1 fallbacks), skip mipmapping.
    const bool generateMips = (numLevels > 1 && w > 1 && h > 1);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = sRGB ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = w;
    info.height = h;
    info.layer_count_or_depth = 1;
    info.num_levels = generateMips ? numLevels : 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    // SAMPLER for sampling in shaders; COLOR_TARGET needed as blit destination for mip generation.
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (generateMips ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &info);
    if (!tex) {
        SDL_Log("Renderer: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return nullptr;
    }

    // Upload base level (mip 0).
    const Uint32 dataSize = w * h * 4u;
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
    dst.w = w;
    dst.h = h;
    dst.d = 1;

    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUTransferBuffer(device, tb);

    // Generate mip chain using GPU blit (linear filter downsampling).
    if (generateMips) {
        Uint32 mipW = w;
        Uint32 mipH = h;
        for (Uint32 mip = 1; mip < numLevels; ++mip) {
            Uint32 prevW = mipW;
            Uint32 prevH = mipH;
            mipW = std::max(mipW >> 1, 1u);
            mipH = std::max(mipH >> 1, 1u);

            SDL_GPUCommandBuffer* blitCmd = SDL_AcquireGPUCommandBuffer(device);

            SDL_GPUBlitInfo blit{};
            blit.source.texture = tex;
            blit.source.mip_level = mip - 1;
            blit.source.w = prevW;
            blit.source.h = prevH;
            blit.destination.texture = tex;
            blit.destination.mip_level = mip;
            blit.destination.w = mipW;
            blit.destination.h = mipH;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;

            SDL_BlitGPUTexture(blitCmd, &blit);
            SDL_SubmitGPUCommandBuffer(blitCmd);
        }
        SDL_WaitForGPUIdle(device);
    }

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
    sampInfo.max_lod = 13.0f; // Allow up to 8192×8192 mip chain (log2(8192)+1=13).
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
// Post-processing init (Phases 7-13)
// ═══════════════════════════════════════════════════════════════════════════

bool Renderer::initBloom()
{
    // Bloom downsample: 1 sampler, 1 rw storage tex, 1 UBO, workgroup 16×16.
    bloomDownsamplePipeline = createComputePipeline("bloom_downsample.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    // Bloom upsample: 1 sampler, 1 rw storage tex (read+write), 1 UBO.
    bloomUpsamplePipeline = createComputePipeline("bloom_upsample.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    return bloomDownsamplePipeline && bloomUpsamplePipeline;
}

bool Renderer::initSSAO()
{
    // SSAO: 1 sampler (depth), 1 rw storage tex (ssao output), 1 UBO.
    ssaoPipeline = createComputePipeline("ssao.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    // SSAO blur: 1 sampler, 1 rw storage tex, 0 UBO.
    ssaoBlurPipeline = createComputePipeline("ssao_blur.comp", 1, 0, 0, 1, 0, 0, 16, 16, 1);
    return ssaoPipeline && ssaoBlurPipeline;
}

bool Renderer::initSSR()
{
    // SSR: 4 samplers (hdr + depth + prevSSR + motionVectors), 1 rw storage tex, 1 UBO.
    ssrPipeline = createComputePipeline("ssr.comp", 4, 0, 0, 1, 0, 1, 16, 16, 1);
    return ssrPipeline != nullptr;
}

bool Renderer::initVolumetrics()
{
    // Volumetric: 2 samplers (depth + shadow), 1 rw storage tex, 1 UBO.
    volumetricPipeline = createComputePipeline("volumetric.comp", 2, 0, 0, 1, 0, 1, 16, 16, 1);
    return volumetricPipeline != nullptr;
}

bool Renderer::initTAA()
{
    // Motion vectors: 1 sampler (depth), 1 rw storage tex, 1 UBO.
    motionVectorPipeline = createComputePipeline("motion_vectors.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    // TAA: 3 samplers (current + history + motion), 1 rw storage tex, 1 UBO.
    taaPipeline = createComputePipeline("taa.comp", 3, 0, 0, 1, 0, 1, 16, 16, 1);
    return motionVectorPipeline && taaPipeline;
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
    // Shadow pipeline + shadow map texture + comparison sampler.
    initShadowPipeline();
    {
        // Shadow map: D32_FLOAT, 2048×2048, single layer (no cascades yet).
        SDL_GPUTextureCreateInfo smInfo{};
        smInfo.type = SDL_GPU_TEXTURETYPE_2D;
        smInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        smInfo.width = k_shadowMapSize;
        smInfo.height = k_shadowMapSize;
        smInfo.layer_count_or_depth = 1;
        smInfo.num_levels = 1;
        smInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        smInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        shadowMap = SDL_CreateGPUTexture(device, &smInfo);
        if (!shadowMap)
            SDL_Log("Renderer: shadow map creation failed: %s", SDL_GetError());

        // Comparison sampler for PCF shadow sampling.
        SDL_GPUSamplerCreateInfo ssInfo{};
        ssInfo.min_filter = SDL_GPU_FILTER_LINEAR;
        ssInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
        ssInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ssInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ssInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ssInfo.enable_compare = true;
        ssInfo.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        shadowSampler = SDL_CreateGPUSampler(device, &ssInfo);
    }

    // Generate IBL textures (BRDF LUT, irradiance map, prefilter map).
    if (!initIBL())
        SDL_Log("Renderer: IBL init failed — metallic surfaces will appear dark");

    // ── Post-processing compute pipelines (Phases 7-13) ─────────────────────
    if (!initBloom())
        SDL_Log("Renderer: bloom init failed");
    if (!initSSAO())
        SDL_Log("Renderer: SSAO init failed");
    if (!initSSR())
        SDL_Log("Renderer: SSR init failed");
    if (!initVolumetrics())
        SDL_Log("Renderer: volumetrics init failed");
    if (!initTAA())
        SDL_Log("Renderer: TAA init failed");

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
    // Y offset +1.3 compensates for the model's wheels extending below its origin.
    // Without this, the wheels clip through the floor and reflections appear detached.
    loadAndPlace("free_1975_porsche_911_930_turbo.glb", glm::vec3(-200.0f, 1.3f, 400.0f), 40.0f, true);
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

    // ── Ensure post-processing textures exist at screen resolution ───────
    // Bloom mip chain (lazy create/resize).
    {
        Uint32 mipW = w / 2, mipH = h / 2;
        for (int i = 0; i < k_bloomMips; ++i) {
            if (!bloomMips[i] || true) { // Always recreate for simplicity.
                if (bloomMips[i])
                    SDL_ReleaseGPUTexture(device, bloomMips[i]);
                SDL_GPUTextureCreateInfo ci{};
                ci.type = SDL_GPU_TEXTURETYPE_2D;
                ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
                ci.width = std::max(mipW, 1u);
                ci.height = std::max(mipH, 1u);
                ci.layer_count_or_depth = 1;
                ci.num_levels = 1;
                ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE |
                           SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
                bloomMips[i] = SDL_CreateGPUTexture(device, &ci);
            }
            mipW = std::max(mipW / 2, 1u);
            mipH = std::max(mipH / 2, 1u);
        }
    }

    // SSAO textures.
    if (!ssaoTexture) {
        auto makeR8 = [&](Uint32 tw, Uint32 th) -> SDL_GPUTexture* {
            SDL_GPUTextureCreateInfo ci{};
            ci.type = SDL_GPU_TEXTURETYPE_2D;
            ci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
            ci.width = tw;
            ci.height = th;
            ci.layer_count_or_depth = 1;
            ci.num_levels = 1;
            ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
            return SDL_CreateGPUTexture(device, &ci);
        };
        ssaoTexture = makeR8(w, h);
        ssaoBlurTexture = makeR8(w, h);
    }

    // SSR, volumetric, TAA, motion vectors (lazy init once).
    auto makeRGBA16F = [&](Uint32 tw, Uint32 th) -> SDL_GPUTexture* {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.width = tw;
        ci.height = th;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        return SDL_CreateGPUTexture(device, &ci);
    };

    if (!ssrTexture[0]) {
        ssrTexture[0] = makeRGBA16F(w, h);
        ssrTexture[1] = makeRGBA16F(w, h);
    }
    if (!volumetricTexture)
        volumetricTexture = makeRGBA16F(w / 2, h / 2);
    if (!taaHistory[0]) {
        taaHistory[0] = makeRGBA16F(w, h);
        taaHistory[1] = makeRGBA16F(w, h);
    }
    if (!motionVectorTexture) {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
        ci.width = w;
        ci.height = h;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        motionVectorTexture = SDL_CreateGPUTexture(device, &ci);
    }

    camera.setAspect((h != 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f);

    // ── Upload particle data (BEFORE any render pass) ────────────────────
    if (particleSystem && toggles.particles)
        particleSystem->uploadToGpu(cmd);

    // ── Prepare ImGui ───────────────────────────────────────────────────────
    ImDrawData* const drawData = ImGui::GetDrawData();
    if (drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);

    // ════════════════════════════════════════════════════════════════════════
    // PASS 0: Shadow map — depth-only from directional light's perspective
    // ════════════════════════════════════════════════════════════════════════
    glm::mat4 lightVP(1.0f);
    if (toggles.shadows && shadowPipeline && shadowMap) {
        // Light direction matches the primary directional light.
        const glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f));
        // Place the light "camera" far along the light direction, looking at scene center.
        const glm::vec3 lightPos = lightDir * 1500.0f;
        const glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f, 0.0f, 400.0f), glm::vec3(0, 1, 0));
        // Orthographic projection covering the scene.
        const glm::mat4 lightProj = glm::ortho(-600.0f, 600.0f, -300.0f, 300.0f, 0.1f, 3000.0f);
        lightVP = lightProj * lightView;

        SDL_GPUDepthStencilTargetInfo sdt{};
        sdt.texture = shadowMap;
        sdt.clear_depth = 1.0f;
        sdt.load_op = SDL_GPU_LOADOP_CLEAR;
        sdt.store_op = SDL_GPU_STOREOP_STORE;
        sdt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        sdt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* shadowPass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &sdt);
        SDL_BindGPUGraphicsPipeline(shadowPass, shadowPipeline);

        // Render all opaque scene model meshes into the shadow map.
        for (const auto& model : models) {
            if (!model.drawInScenePass)
                continue; // entity/weapon models are not part of the static scene shadow
            ShadowUBO shadowUBO{};
            shadowUBO.lightVP = lightVP;
            shadowUBO.model = model.transform;
            SDL_PushGPUVertexUniformData(cmd, 0, &shadowUBO, sizeof(shadowUBO));

            for (const auto& mesh : model.meshes) {
                if (mesh.isTransparent)
                    continue; // skip transparent meshes for shadows
                const SDL_GPUBufferBinding vbBind = {.buffer = mesh.vertexBuffer, .offset = 0};
                SDL_BindGPUVertexBuffers(shadowPass, 0, &vbBind, 1);
                const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
                SDL_BindGPUIndexBuffer(shadowPass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                SDL_DrawGPUIndexedPrimitives(shadowPass, mesh.indexCount, 1, 0, 0, 0);
            }
        }

        SDL_EndGPURenderPass(shadowPass);
    }

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

        // ── Scene geometry (physics playground) ──────────────────────────────
        if (toggles.sceneGeometry && scenePipeline) {
            SceneMatrices sceneMats{};
            sceneMats.model = glm::mat4(1.0f);
            sceneMats.view = camera.getView();
            sceneMats.projection = camera.getProjection();
            SDL_PushGPUVertexUniformData(cmd, 0, &sceneMats, sizeof(sceneMats));

            SDL_BindGPUGraphicsPipeline(pass, scenePipeline);
            SDL_DrawGPUPrimitives(pass, 1002, 1, 0, 0);
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

            // Helper: draw all scene-placed meshes matching the transparency filter.
            // Entity/weapon models (drawInScenePass==false) are handled separately.
            auto drawMeshes = [&](bool wantTransparent) {
                for (const auto& model : models) {
                    if (!model.drawInScenePass)
                        continue;
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

                        const SDL_GPUTextureSamplerBinding samplers[8] = {
                            {.texture = resolveTex(mesh.albedoTexIndex, fallbackWhite), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.metallicRoughnessTexIndex, fallbackMR), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.emissiveTexIndex, fallbackBlack), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.normalTexIndex, fallbackFlatNormal), .sampler = pbrSampler},
                            {.texture = irradianceMap, .sampler = iblSampler},
                            {.texture = prefilterMap, .sampler = iblSampler},
                            {.texture = brdfLUT, .sampler = iblSampler},
                            {.texture = shadowMap ? shadowMap : fallbackWhite,
                             .sampler = shadowSampler ? shadowSampler : pbrSampler},
                        };
                        SDL_BindGPUFragmentSamplers(pass, 0, samplers, 8);

                        SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, 1, 0, 0, 0);
                    }
                }
            };

            // Shadow data UBO (shared by all meshes).
            ShadowDataFragUBO shadowData{};
            shadowData.lightVP = lightVP;
            shadowData.shadowBias = 0.002f;
            shadowData.shadowNormalBias = 0.01f;
            shadowData.shadowMapSize = (shadowMap && shadowPipeline) ? static_cast<float>(k_shadowMapSize) : 0.0f;

            // Pass 1: Opaque meshes (writes depth, no blending).
            if (toggles.pbrModels && pbrPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, pbrPipeline);
                SDL_PushGPUFragmentUniformData(cmd, 1, &lightData, sizeof(lightData));
                SDL_PushGPUFragmentUniformData(cmd, 2, &shadowData, sizeof(shadowData));
                drawMeshes(false);
            }

            // Pass 2: Skybox — BEFORE transparents so transparent fragments
            // blend with the sky colour, not the black clear colour.
            if (toggles.skybox && skyboxPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, skyboxPipeline);

                SkyboxMatricesUBO skyMats{};
                glm::mat4 viewRot = camera.getView();
                viewRot[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                skyMats.viewRotation = viewRot;
                skyMats.projection = camera.getProjection();
                SDL_PushGPUVertexUniformData(cmd, 0, &skyMats, sizeof(skyMats));

                SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
            }

            // ── Entity render commands (PBR models at entity positions) ─────
            // These are driven by the ECS — each entity with Renderable + Position
            // contributes an EntityRenderCmd built by Game::iterate().
            if (toggles.entityModels && pbrPipeline && !entityRenderCmds.empty()) {
                SDL_BindGPUGraphicsPipeline(pass, pbrPipeline);
                SDL_PushGPUFragmentUniformData(cmd, 1, &lightData, sizeof(lightData));
                SDL_PushGPUFragmentUniformData(cmd, 2, &shadowData, sizeof(shadowData));

                for (const auto& ecmd : entityRenderCmds) {
                    if (ecmd.modelIndex < 0 || ecmd.modelIndex >= static_cast<int>(models.size()))
                        continue;

                    const auto& emodel = models[static_cast<size_t>(ecmd.modelIndex)];

                    Matrices entMats{};
                    entMats.model = ecmd.worldTransform;
                    entMats.view = camera.getView();
                    entMats.projection = camera.getProjection();
                    entMats.normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(ecmd.worldTransform)));
                    SDL_PushGPUVertexUniformData(cmd, 0, &entMats, sizeof(entMats));

                    for (const auto& mesh : emodel.meshes) {
                        if (mesh.isTransparent)
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
                            if (idx >= 0 && static_cast<size_t>(idx) < emodel.textures.size() &&
                                emodel.textures[static_cast<size_t>(idx)])
                                return emodel.textures[static_cast<size_t>(idx)];
                            return fallback;
                        };

                        const SDL_GPUTextureSamplerBinding samplers[8] = {
                            {.texture = resolveTex(mesh.albedoTexIndex, fallbackWhite), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.metallicRoughnessTexIndex, fallbackMR), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.emissiveTexIndex, fallbackBlack), .sampler = pbrSampler},
                            {.texture = resolveTex(mesh.normalTexIndex, fallbackFlatNormal), .sampler = pbrSampler},
                            {.texture = irradianceMap, .sampler = iblSampler},
                            {.texture = prefilterMap, .sampler = iblSampler},
                            {.texture = brdfLUT, .sampler = iblSampler},
                            {.texture = shadowMap ? shadowMap : fallbackWhite,
                             .sampler = shadowSampler ? shadowSampler : pbrSampler},
                        };
                        SDL_BindGPUFragmentSamplers(pass, 0, samplers, 8);

                        SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, 1, 0, 0, 0);
                    }
                }
            }

            // Pass 3: Transparent meshes (alpha blending, no depth write).
            // Rendered after skybox so they blend with the sky background.
            if (toggles.pbrModels && pbrTransparentPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, pbrTransparentPipeline);
                SDL_PushGPUFragmentUniformData(cmd, 1, &lightData, sizeof(lightData));
                SDL_PushGPUFragmentUniformData(cmd, 2, &shadowData, sizeof(shadowData));
                drawMeshes(true);
            }
        }

        // ── Particle rendering (inside HDR pass, after opaques + skybox) ─────
        // Push ParticleUniforms matching the layout expected by all particle
        // vertex shaders (set=1, binding=0): view, proj, camPos, camRight, camUp.
        if (toggles.particles && particleSystem) {
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

            ParticleUniforms pu{};
            pu.view = camera.getView();
            pu.proj = camera.getProjection();
            pu.camPos = camera.getEye();
            pu.camRight = camera.getRight();
            pu.camUp = camera.getUp();
            SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));

            particleSystem->setScreenSize(static_cast<float>(w), static_cast<float>(h));
            particleSystem->render(pass, cmd);
        }

        // ── First-person weapon viewmodel ────────────────────────────────────
        // Rendered last in the HDR pass. The weapon should always be in front,
        // so we don't clear depth — it just draws over the scene at close range.
        if (toggles.weaponViewmodel && weaponVM.visible && weaponVM.modelIndex >= 0 &&
            weaponVM.modelIndex < static_cast<int>(models.size()) && pbrPipeline)
        {

            SDL_BindGPUGraphicsPipeline(pass, pbrPipeline);

            const auto& wmodel = models[static_cast<size_t>(weaponVM.modelIndex)];

            Matrices vmMats{};
            vmMats.model = weaponVM.transform;
            vmMats.view = camera.getView();
            vmMats.projection = camera.getProjection();
            vmMats.normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(weaponVM.transform)));
            SDL_PushGPUVertexUniformData(cmd, 0, &vmMats, sizeof(vmMats));

            // Light data for weapon (same as scene).
            LightDataUBO weaponLightData{};
            weaponLightData.cameraPos = glm::vec4(eye, 1.0f);
            weaponLightData.ambientColor = glm::vec4(0.08f, 0.08f, 0.10f, 1.0f);
            weaponLightData.numLights = 2;
            weaponLightData.lights[0].position = glm::vec4(glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f)), 0.0f);
            weaponLightData.lights[0].color = glm::vec4(1.0f, 0.95f, 0.85f, 3.0f);
            weaponLightData.lights[1].position = glm::vec4(glm::normalize(glm::vec3(-0.5f, 0.3f, -0.8f)), 0.0f);
            weaponLightData.lights[1].color = glm::vec4(0.3f, 0.4f, 0.6f, 1.0f);
            SDL_PushGPUFragmentUniformData(cmd, 1, &weaponLightData, sizeof(weaponLightData));

            ShadowDataFragUBO weaponShadow{};
            weaponShadow.shadowMapSize = 0.0f; // No shadows for weapon viewmodel.
            SDL_PushGPUFragmentUniformData(cmd, 2, &weaponShadow, sizeof(weaponShadow));

            for (const auto& mesh : wmodel.meshes) {
                if (mesh.isTransparent)
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
                    if (idx >= 0 && static_cast<size_t>(idx) < wmodel.textures.size() &&
                        wmodel.textures[static_cast<size_t>(idx)])
                        return wmodel.textures[static_cast<size_t>(idx)];
                    return fallback;
                };

                const SDL_GPUTextureSamplerBinding samplers[8] = {
                    {.texture = resolveTex(mesh.albedoTexIndex, fallbackWhite), .sampler = pbrSampler},
                    {.texture = resolveTex(mesh.metallicRoughnessTexIndex, fallbackMR), .sampler = pbrSampler},
                    {.texture = resolveTex(mesh.emissiveTexIndex, fallbackBlack), .sampler = pbrSampler},
                    {.texture = resolveTex(mesh.normalTexIndex, fallbackFlatNormal), .sampler = pbrSampler},
                    {.texture = irradianceMap, .sampler = iblSampler},
                    {.texture = prefilterMap, .sampler = iblSampler},
                    {.texture = brdfLUT, .sampler = iblSampler},
                    {.texture = shadowMap ? shadowMap : fallbackWhite,
                     .sampler = shadowSampler ? shadowSampler : pbrSampler},
                };
                SDL_BindGPUFragmentSamplers(pass, 0, samplers, 8);

                SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, 1, 0, 0, 0);
            }
        }

        SDL_EndGPURenderPass(pass);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Compute passes: SSAO, Bloom, SSR, Volumetrics (between HDR and tonemap)
    // ════════════════════════════════════════════════════════════════════════

    // ── SSAO (Phase 7) ──────────────────────────────────────────────────────
    if (toggles.ssao && ssaoPipeline && ssaoBlurPipeline && ssaoTexture && ssaoBlurTexture && depthTexture) {
        struct
        {
            glm::mat4 proj;
            glm::mat4 invProj;
            glm::vec4 kernel[32];
            glm::vec2 noiseScale;
            float radius;
            float bias;
        } ssaoUBO{};
        ssaoUBO.proj = camera.getProjection();
        ssaoUBO.invProj = glm::inverse(camera.getProjection());
        ssaoUBO.noiseScale = glm::vec2(static_cast<float>(w) / 4.0f, static_cast<float>(h) / 4.0f);
        ssaoUBO.radius = 50.0f; // world units
        ssaoUBO.bias = 1.0f;
        // Generate hemisphere kernel.
        for (int i = 0; i < 32; ++i) {
            float xi1 = static_cast<float>(i) / 32.0f;
            float xi2 = static_cast<float>((i * 7 + 3) % 32) / 32.0f;
            float xi3 = static_cast<float>((i * 13 + 5) % 32) / 32.0f;
            glm::vec3 s(xi1 * 2.0f - 1.0f, xi2 * 2.0f - 1.0f, xi3);
            s = glm::normalize(s) * (0.1f + 0.9f * xi3 * xi3); // bias toward center
            ssaoUBO.kernel[i] = glm::vec4(s, 0.0f);
        }

        // SSAO compute pass.
        SDL_GPUStorageTextureReadWriteBinding ssaoWrite = {.texture = ssaoTexture, .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* ssaoPass = SDL_BeginGPUComputePass(cmd, &ssaoWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(ssaoPass, ssaoPipeline);
        SDL_GPUTextureSamplerBinding depthSamp = {.texture = depthTexture, .sampler = tonemapSampler};
        SDL_BindGPUComputeSamplers(ssaoPass, 0, &depthSamp, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &ssaoUBO, sizeof(ssaoUBO));
        SDL_DispatchGPUCompute(ssaoPass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(ssaoPass);

        // SSAO blur pass.
        SDL_GPUStorageTextureReadWriteBinding ssaoBlurWrite = {.texture = ssaoBlurTexture, .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* ssaoBlurPass = SDL_BeginGPUComputePass(cmd, &ssaoBlurWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(ssaoBlurPass, ssaoBlurPipeline);
        SDL_GPUTextureSamplerBinding ssaoSamp = {.texture = ssaoTexture, .sampler = tonemapSampler};
        SDL_BindGPUComputeSamplers(ssaoBlurPass, 0, &ssaoSamp, 1);
        SDL_DispatchGPUCompute(ssaoBlurPass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(ssaoBlurPass);
    }

    // ── Bloom (Phase 8) ─────────────────────────────────────────────────────
    if (toggles.bloom && bloomDownsamplePipeline && bloomUpsamplePipeline && bloomMips[0]) {
        // Downsample chain.
        Uint32 srcW = w, srcH = h;
        for (int i = 0; i < k_bloomMips; ++i) {
            Uint32 dstW = std::max(srcW / 2, 1u);
            Uint32 dstH = std::max(srcH / 2, 1u);

            struct
            {
                glm::vec2 srcRes;
                float isFirstPass;
                float _p;
            } params{};
            params.srcRes = glm::vec2(static_cast<float>(srcW), static_cast<float>(srcH));
            params.isFirstPass = (i == 0) ? 1.0f : 0.0f;

            SDL_GPUStorageTextureReadWriteBinding dstWrite = {.texture = bloomMips[i], .mip_level = 0, .layer = 0};
            SDL_GPUComputePass* bloomPass = SDL_BeginGPUComputePass(cmd, &dstWrite, 1, nullptr, 0);
            SDL_BindGPUComputePipeline(bloomPass, bloomDownsamplePipeline);
            SDL_GPUTextureSamplerBinding srcSamp = {.texture = (i == 0) ? hdrTarget : bloomMips[i - 1],
                                                    .sampler = tonemapSampler};
            SDL_BindGPUComputeSamplers(bloomPass, 0, &srcSamp, 1);
            SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
            SDL_DispatchGPUCompute(bloomPass, (dstW + 15) / 16, (dstH + 15) / 16, 1);
            SDL_EndGPUComputePass(bloomPass);

            srcW = dstW;
            srcH = dstH;
        }

        // Upsample chain (additive blend back up).
        for (int i = k_bloomMips - 2; i >= 0; --i) {
            Uint32 mipW = std::max(w >> (i + 1), 1u);
            Uint32 mipH = std::max(h >> (i + 1), 1u);

            struct
            {
                glm::vec2 srcRes;
                float intensity;
                float _p;
            } params{};
            params.srcRes = glm::vec2(static_cast<float>(std::max(w >> (i + 2), 1u)),
                                      static_cast<float>(std::max(h >> (i + 2), 1u)));
            params.intensity = 0.5f;

            SDL_GPUStorageTextureReadWriteBinding dstWrite = {.texture = bloomMips[i], .mip_level = 0, .layer = 0};
            SDL_GPUComputePass* upPass = SDL_BeginGPUComputePass(cmd, &dstWrite, 1, nullptr, 0);
            SDL_BindGPUComputePipeline(upPass, bloomUpsamplePipeline);
            SDL_GPUTextureSamplerBinding srcSamp = {.texture = bloomMips[i + 1], .sampler = tonemapSampler};
            SDL_BindGPUComputeSamplers(upPass, 0, &srcSamp, 1);
            SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
            SDL_DispatchGPUCompute(upPass, (mipW + 15) / 16, (mipH + 15) / 16, 1);
            SDL_EndGPUComputePass(upPass);
        }
    }

    // ── SSR (Phase 9) ───────────────────────────────────────────────────────
    static uint64_t ssrFrameCounter = 0;
    ++ssrFrameCounter;

    if (toggles.ssr && ssrPipeline && ssrTexture[0] && depthTexture && hdrTarget) {
        // Ping-pong: write to current, read history from previous.
        const int ssrSrc = ssrCurrentIdx;     // previous frame's result
        const int ssrDst = 1 - ssrCurrentIdx; // this frame's output
        struct
        {
            glm::mat4 proj;
            glm::mat4 invProj;
            glm::mat4 view;
            glm::vec2 screenSize;
            float maxDist;
            float thickness;
            float frameIndex;
            float jitterStrength;
            int ssrModeVal;
            float _pad1, _pad2, _pad3;
        } ssrUBO{};
        ssrUBO.proj = camera.getProjection();
        ssrUBO.invProj = glm::inverse(camera.getProjection());
        ssrUBO.view = camera.getView();
        ssrUBO.screenSize = glm::vec2(static_cast<float>(w), static_cast<float>(h));
        ssrUBO.maxDist = 500.0f;
        ssrUBO.thickness = 5.0f;
        ssrUBO.frameIndex = static_cast<float>(ssrFrameCounter % 64);
        ssrUBO.jitterStrength = 0.06f;
        ssrUBO.ssrModeVal = ssrMode;

        SDL_GPUStorageTextureReadWriteBinding ssrWrite = {.texture = ssrTexture[ssrDst], .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* ssrPass = SDL_BeginGPUComputePass(cmd, &ssrWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(ssrPass, ssrPipeline);
        SDL_GPUTextureSamplerBinding ssrSamplers[4] = {
            {.texture = hdrTarget, .sampler = tonemapSampler},
            {.texture = depthTexture, .sampler = tonemapSampler},
            {.texture = ssrTexture[ssrSrc], .sampler = tonemapSampler},
            {.texture = motionVectorTexture ? motionVectorTexture : fallbackBlack, .sampler = tonemapSampler},
        };
        SDL_BindGPUComputeSamplers(ssrPass, 0, ssrSamplers, 4);
        SDL_PushGPUComputeUniformData(cmd, 0, &ssrUBO, sizeof(ssrUBO));
        SDL_DispatchGPUCompute(ssrPass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(ssrPass);
        ssrCurrentIdx = ssrDst;
    }

    // ── Volumetrics (Phase 10) ──────────────────────────────────────────────
    if (toggles.volumetrics && volumetricPipeline && volumetricTexture && depthTexture && shadowMap && shadowSampler) {
        struct
        {
            glm::mat4 invViewProj;
            glm::mat4 lightVP_vol;
            glm::vec4 lightDir_vol;
            glm::vec4 lightColor_vol;
            glm::vec2 screenSize;
            float fogDensity;
            float scatteringG;
            float shadowBias_vol;
            float maxDistance;
            float _p1;
            float _p2;
        } volUBO{};
        volUBO.invViewProj = glm::inverse(camera.getViewProjection());
        volUBO.lightVP_vol = lightVP;
        volUBO.lightDir_vol = glm::vec4(glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f)), 0.0f);
        volUBO.lightColor_vol = glm::vec4(1.0f, 0.95f, 0.85f, 2.0f);
        volUBO.screenSize = glm::vec2(static_cast<float>(w), static_cast<float>(h));
        volUBO.fogDensity = 0.001f;
        volUBO.scatteringG = 0.7f;
        volUBO.shadowBias_vol = 0.002f;
        volUBO.maxDistance = 2000.0f;

        SDL_GPUStorageTextureReadWriteBinding volWrite = {.texture = volumetricTexture, .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* volPass = SDL_BeginGPUComputePass(cmd, &volWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(volPass, volumetricPipeline);
        SDL_GPUTextureSamplerBinding volSamplers[2] = {
            {.texture = depthTexture, .sampler = tonemapSampler},
            {.texture = shadowMap, .sampler = shadowSampler},
        };
        SDL_BindGPUComputeSamplers(volPass, 0, volSamplers, 2);
        SDL_PushGPUComputeUniformData(cmd, 0, &volUBO, sizeof(volUBO));
        SDL_DispatchGPUCompute(volPass, (w / 2 + 15) / 16, (h / 2 + 15) / 16, 1);
        SDL_EndGPUComputePass(volPass);
    }

    // ── TAA (Phase 11) — motion vectors + temporal resolve ────────────────
    if (toggles.taa && motionVectorPipeline && taaPipeline && motionVectorTexture && taaHistory[0] && taaHistory[1] &&
        depthTexture)
    {
        const glm::mat4 currentVP = camera.getViewProjection();

        // Motion vectors.
        struct
        {
            glm::mat4 curInvVP;
            glm::mat4 prevVP;
            glm::vec2 screenSize;
            glm::vec2 jitter;
        } mvUBO{};
        mvUBO.curInvVP = glm::inverse(currentVP);
        mvUBO.prevVP = previousVP;
        mvUBO.screenSize = glm::vec2(static_cast<float>(w), static_cast<float>(h));

        SDL_GPUStorageTextureReadWriteBinding mvWrite = {.texture = motionVectorTexture, .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* mvPass = SDL_BeginGPUComputePass(cmd, &mvWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(mvPass, motionVectorPipeline);
        SDL_GPUTextureSamplerBinding mvSamp = {.texture = depthTexture, .sampler = tonemapSampler};
        SDL_BindGPUComputeSamplers(mvPass, 0, &mvSamp, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &mvUBO, sizeof(mvUBO));
        SDL_DispatchGPUCompute(mvPass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(mvPass);

        // TAA temporal resolve.
        const int srcIdx = taaCurrentIdx;
        const int dstIdx = 1 - taaCurrentIdx;

        struct
        {
            glm::vec2 screenSize;
            float blendFactor;
            float _p;
        } taaUBO{};
        taaUBO.screenSize = glm::vec2(static_cast<float>(w), static_cast<float>(h));
        taaUBO.blendFactor = 0.1f;

        SDL_GPUStorageTextureReadWriteBinding taaWrite = {.texture = taaHistory[dstIdx], .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* taaPass = SDL_BeginGPUComputePass(cmd, &taaWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(taaPass, taaPipeline);
        SDL_GPUTextureSamplerBinding taaSamplers[3] = {
            {.texture = hdrTarget, .sampler = tonemapSampler},
            {.texture = taaHistory[srcIdx], .sampler = tonemapSampler},
            {.texture = motionVectorTexture, .sampler = tonemapSampler},
        };
        SDL_BindGPUComputeSamplers(taaPass, 0, taaSamplers, 3);
        SDL_PushGPUComputeUniformData(cmd, 0, &taaUBO, sizeof(taaUBO));
        SDL_DispatchGPUCompute(taaPass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(taaPass);

        taaCurrentIdx = dstIdx;
        previousVP = currentVP;
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
            params.bloomStrength = bloomMips[0] ? 0.3f : 0.0f;
            params.ssaoStrength = ssaoBlurTexture ? 0.5f : 0.0f;
            params.ssrStrength = ssrTexture[0] ? 0.4f : 0.0f;
            params.volumetricStrength = volumetricTexture ? 0.5f : 0.0f;
            SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));

            // Bind all 5 post-process textures for compositing.
            const SDL_GPUTextureSamplerBinding tonemapSamplers[5] = {
                {.texture = hdrTarget, .sampler = tonemapSampler},
                {.texture = bloomMips[0] ? bloomMips[0] : fallbackBlack, .sampler = tonemapSampler},
                {.texture = ssaoBlurTexture ? ssaoBlurTexture : fallbackWhite, .sampler = tonemapSampler},
                {.texture = ssrTexture[ssrCurrentIdx] ? ssrTexture[ssrCurrentIdx] : fallbackBlack,
                 .sampler = tonemapSampler},
                {.texture = volumetricTexture ? volumetricTexture : fallbackBlack, .sampler = tonemapSampler},
            };
            SDL_BindGPUFragmentSamplers(pass, 0, tonemapSamplers, 5);

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

int Renderer::loadSceneModel(const char* filename, glm::vec3 pos, float scale, bool flipUVs)
{
    const char* const k_base = SDL_GetBasePath();
    char path[512];
    SDL_snprintf(path, sizeof(path), "%sassets/%s", k_base ? k_base : "", filename);

    LoadedModel loaded;
    if (!loadModel(path, loaded, flipUVs)) {
        SDL_Log("Renderer::loadSceneModel: failed to load '%s'", filename);
        return -1;
    }

    ModelInstance inst;
    inst.transform = glm::scale(glm::translate(glm::mat4(1.0f), pos), glm::vec3(scale));
    inst.drawInScenePass = false; // Only drawn via EntityRenderCmd or WeaponViewmodel.

    if (!uploadModel(loaded, inst)) {
        SDL_Log("Renderer::loadSceneModel: GPU upload failed for '%s'", filename);
        return -1;
    }

    models.push_back(std::move(inst));
    return static_cast<int>(models.size()) - 1;
}

int Renderer::uploadSceneModel(const LoadedModel& model)
{
    ModelInstance inst;
    inst.transform = glm::mat4(1.0f);
    inst.drawInScenePass = false; // Only drawn via EntityRenderCmd.

    if (!uploadModel(model, inst)) {
        SDL_Log("Renderer::uploadSceneModel: GPU upload failed");
        return -1;
    }

    models.push_back(std::move(inst));
    return static_cast<int>(models.size()) - 1;
}

void Renderer::updateModelMeshVertices(int modelIndex, int meshIndex, const ModelVertex* vertices, Uint32 vertexCount)
{
    if (modelIndex < 0 || static_cast<size_t>(modelIndex) >= models.size())
        return;
    auto& model = models[static_cast<size_t>(modelIndex)];
    if (meshIndex < 0 || static_cast<size_t>(meshIndex) >= model.meshes.size())
        return;

    const Uint32 bytes = vertexCount * static_cast<Uint32>(sizeof(ModelVertex));

    // Staging transfer buffer — created per call, released after GPU copy.
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb)
        return;

    void* mapped = SDL_MapGPUTransferBuffer(device, tb, false);
    SDL_memcpy(mapped, vertices, bytes);
    SDL_UnmapGPUTransferBuffer(device, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    src.offset = 0;

    SDL_GPUBufferRegion dst{};
    dst.buffer = model.meshes[static_cast<size_t>(meshIndex)].vertexBuffer;
    dst.offset = 0;
    dst.size = bytes;

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);

    // Safe to release immediately — SDL defers actual free until the GPU is done.
    SDL_ReleaseGPUTransferBuffer(device, tb);
}

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

    // Release post-processing resources.
    for (auto*& t : bloomMips) {
        if (t)
            SDL_ReleaseGPUTexture(device, t);
        t = nullptr;
    }
    if (ssaoTexture)
        SDL_ReleaseGPUTexture(device, ssaoTexture);
    if (ssaoBlurTexture)
        SDL_ReleaseGPUTexture(device, ssaoBlurTexture);
    for (auto*& t : ssrTexture) {
        if (t)
            SDL_ReleaseGPUTexture(device, t);
        t = nullptr;
    }
    if (volumetricTexture)
        SDL_ReleaseGPUTexture(device, volumetricTexture);
    if (motionVectorTexture)
        SDL_ReleaseGPUTexture(device, motionVectorTexture);
    for (auto*& t : taaHistory) {
        if (t)
            SDL_ReleaseGPUTexture(device, t);
        t = nullptr;
    }

    if (bloomDownsamplePipeline)
        SDL_ReleaseGPUComputePipeline(device, bloomDownsamplePipeline);
    if (bloomUpsamplePipeline)
        SDL_ReleaseGPUComputePipeline(device, bloomUpsamplePipeline);
    if (ssaoPipeline)
        SDL_ReleaseGPUComputePipeline(device, ssaoPipeline);
    if (ssaoBlurPipeline)
        SDL_ReleaseGPUComputePipeline(device, ssaoBlurPipeline);
    if (ssrPipeline)
        SDL_ReleaseGPUComputePipeline(device, ssrPipeline);
    if (volumetricPipeline)
        SDL_ReleaseGPUComputePipeline(device, volumetricPipeline);
    if (motionVectorPipeline)
        SDL_ReleaseGPUComputePipeline(device, motionVectorPipeline);
    if (taaPipeline)
        SDL_ReleaseGPUComputePipeline(device, taaPipeline);

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
