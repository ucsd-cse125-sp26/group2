/// @file Renderer.cpp
/// @brief SDL3 GPU renderer implementation -- pipelines, passes, and post-processing.

#include "Renderer.hpp"

#include "Camera.hpp"
#include "ModelLoader.hpp"
#include "SMAAAreaTex.h"
#include "SMAASearchTex.h"
#include "particles/ParticleSystem.hpp"

#include <algorithm>
#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <filesystem>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <imgui.h>
#include <stb_image.h>
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

// UBO structures (must match shader layouts exactly)

/// @brief Vertex UBO (set 1, binding 0) -- shared by scene + PBR pipelines.
struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 normalMatrix; ///< transpose(inverse(model)), padded to mat4 for std140.
};

/// @brief Fragment UBO slot 0 -- per-mesh PBR material.
struct MaterialUBO
{
    glm::vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float aoStrength;
    float normalScale;
    glm::vec4 emissiveFactor;
};

/// @brief One light in the LightData UBO.
struct LightGPU
{
    glm::vec4 position; // xyz = direction/position, w = type (0 dir, 1 point)
    glm::vec4 color;    // rgb = colour, a = intensity
    glm::vec4 params;   // x = range, y = innerCone, z = outerCone, w = castsShadow
};

/// @brief Fragment UBO slot 1 -- scene lighting.
///
/// Layout matches the `LightData` uniform block in pbr.frag.  The two floats
/// following `numLights` carry the per-scene IBL intensity multipliers so
/// the user can tame the over-glossy appearance dielectrics get under bright
/// HDR environments without touching shaders.
struct LightDataUBO
{
    glm::vec4 cameraPos;
    glm::vec4 ambientColor;
    int numLights;
    float iblDiffuseIntensity;
    float iblSpecularIntensity;
    float _pad3;
    LightGPU lights[16];
};

/// @brief Skybox vertex UBO.
struct SkyboxMatricesUBO
{
    glm::mat4 viewRotation;
    glm::mat4 projection;
};

/// @brief Shadow map vertex UBO -- matches shadow.vert LightMatrices.
struct ShadowUBO
{
    glm::mat4 lightVP;
    glm::mat4 model;
};

/// @brief Per-cascade data computed each frame.
struct CascadeInfo
{
    glm::mat4 lightView;
    glm::mat4 lightProj;
    glm::mat4 lightVP;
    float splitDistance; ///< View-space far plane of this cascade.
};

/// @brief Shadow data pushed to pbr.frag / normal.frag for cascade shadow sampling.
///
/// The fragment shader selects the cascade based on the fragment's view-space Z.
struct ShadowDataFragUBO
{
    glm::mat4 lightVP[4];    ///< Per-cascade light view-projection matrices.
    glm::vec4 cascadeSplits; ///< View-space far distances for each cascade.
    glm::mat4 cameraView;    ///< Camera view matrix (for computing view-space Z in shader).
    float shadowBias;
    float shadowNormalBias;
    float shadowMapSize;
    float _pad;
    glm::vec4 lightDirWorld;    ///< xyz = direction TO sun.
    glm::vec4 lightColor;       ///< rgb = sun color, a = sun intensity.
    glm::vec4 ambientColor;     ///< rgb = ambient (used by normal.frag).
    glm::vec4 fillColor;        ///< rgb = fill light color, a = fill intensity.
    int numPointLights;         ///< Number of active dynamic point lights (0..14).
    float _pad2, _pad3, _pad4;
    LightGPU scenePtLights[14]; ///< Dynamic point lights for scene geometry.
};

/// @brief Tonemap fragment UBO -- matches tonemap.frag TonemapParams.
struct TonemapParamsUBO
{
    float exposure;
    float gamma;
    int tonemapMode;
    float bloomStrength;
    float ssaoStrength;
    float ssrStrength;
    float volumetricStrength;
    float sharpenStrength;
    float ssaoPower;
    float _padTM1, _padTM2, _padTM3;
};

} // namespace

// Shader loading helper

SDL_GPUShader* Renderer::loadShaderFromFile(const char* name,
                                            SDL_GPUShaderStage stage,
                                            Uint32 samplerCount,
                                            Uint32 uniformBufferCount,
                                            Uint32 storageBufferCount,
                                            Uint32 storageTextureCount)
{
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (shaderFormat == SDL_GPU_SHADERFORMAT_MSL)    ? ".msl"
                              : (shaderFormat == SDL_GPU_SHADERFORMAT_DXIL) ? ".dxil"
                                                                            : ".spv";

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

// Compute pipeline helper

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
    const char* const k_ext = (shaderFormat == SDL_GPU_SHADERFORMAT_MSL)    ? ".msl"
                              : (shaderFormat == SDL_GPU_SHADERFORMAT_DXIL) ? ".dxil"
                                                                            : ".spv";

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

// Pipeline creation

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

    // Transparent PBR pipeline (same shaders, alpha blend, no depth write)
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
        SDL_GPUShader* fragT = loadShaderFromFile("pbr.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 8, 3);

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
    SDL_GPUShader* frag = loadShaderFromFile("skybox.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
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
    // Cull FRONT faces (relative to the light) in the shadow pass — i.e.
    // rasterize the back of each caster.  Halves the shadow rasterization
    // workload AND eliminates most shadow acne on lit surfaces because the
    // recorded depth is on the far side of the caster, not the near side.
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
    // Depth bias to reduce shadow acne.
    pci.rasterizer_state.depth_bias_constant_factor = 0.75f;
    pci.rasterizer_state.depth_bias_slope_factor = 1.0f;
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

// Texture upload

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

// Model upload

bool Renderer::uploadModel(const LoadedModel& model, ModelInstance& outInstance)
{
    if (model.meshes.empty())
        return false;

    // Sampler (created once, shared across all models)
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

    // Fallback textures
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

    // Upload textures
    outInstance.textures.reserve(model.textures.size());
    for (const auto& td : model.textures) {
        SDL_GPUTexture* gpuTex = uploadTexture(td.pixels.data(), td.width, td.height, td.isSRGB);
        outInstance.textures.push_back(gpuTex);
    }

    // Upload geometry
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

// IBL -- generate BRDF LUT, irradiance map, and pre-filtered specular map.
//
// All three are produced by GPU compute shaders:
//   * brdf_lut.comp     -- analytic BRDF integration; runs once at init.
//   * irradiance.comp   -- cosine-weighted convolution of envCubemap.
//   * prefilter.comp    -- GGX-importance-sampled prefilter, per-mip.
//
// Both irradiance and prefilter sample envCubemap, so they're called once at
// init (after uploading the procedural sky) and again whenever a new HDR
// skybox is loaded -- via Renderer::regenerateIBLFromCubemap().

bool Renderer::initIBL()
{
    // Texture resources

    // BRDF LUT (512x512 RG16F). Written by brdf_lut.comp, sampled in pbr.frag.
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
    }

    // Irradiance map work target (32x32 per face, 2D array, RGBA16F).
    // Compute writes one layer at a time here, then we copy the result into the
    // sampled cubemap below. This avoids Metal's restriction that cube texture
    // views must span all 6 faces.
    {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.width = 32;
        ci.height = 32;
        ci.layer_count_or_depth = 6;
        ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        irradianceWorkMap = SDL_CreateGPUTexture(device, &ci);
        if (!irradianceWorkMap) {
            SDL_Log("IBL: failed to create irradiance work map: %s", SDL_GetError());
            return false;
        }
    }

    // Irradiance cubemap sampled by pbr.frag.
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
            SDL_Log("IBL: failed to create irradiance cubemap: %s", SDL_GetError());
            return false;
        }
    }

    // Pre-filter work target (128x128 per face, 5 mip levels, 2D array,
    // RGBA16F). Mip 0 = mirror, mip 4 = roughness 1.0.
    {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.width = 128;
        ci.height = 128;
        ci.layer_count_or_depth = 6;
        ci.num_levels = 5; // mip 0=128, 1=64, 2=32, 3=16, 4=8
        ci.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        prefilterWorkMap = SDL_CreateGPUTexture(device, &ci);
        if (!prefilterWorkMap) {
            SDL_Log("IBL: failed to create prefilter work map: %s", SDL_GetError());
            return false;
        }
    }

    // Pre-filter cubemap sampled by pbr.frag.
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
            SDL_Log("IBL: failed to create prefilter cubemap: %s", SDL_GetError());
            return false;
        }
    }

    // Default environment cubemap (512x512x6 RGBA16F). loadHDRSkybox() may
    // release and recreate this when a new HDR file is loaded; for the
    // procedural fallback we fill it in below.
    {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_CUBE;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.width = 512;
        ci.height = 512;
        ci.layer_count_or_depth = 6;
        ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        envCubemap = SDL_CreateGPUTexture(device, &ci);
        if (!envCubemap) {
            SDL_Log("IBL: failed to create env cubemap: %s", SDL_GetError());
            return false;
        }
    }

    // IBL sampler (linear, clamp, mipmapped)
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

    // Compute pipelines
    //
    // brdf_lut.comp: 0 samplers, 1 RW storage texture, 0 UBOs.
    // irradiance.comp / prefilter.comp: 1 sampler (envCubemap), 1 RW storage
    // texture (output face slice), 1 UBO (face index, plus roughness for
    // prefilter).
    brdfLutPipeline = createComputePipeline("brdf_lut.comp", 0, 0, 0, 1, 0, 0, 16, 16, 1);
    irradiancePipeline = createComputePipeline("irradiance.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    prefilterPipeline = createComputePipeline("prefilter.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    if (!brdfLutPipeline || !irradiancePipeline || !prefilterPipeline) {
        SDL_Log("IBL: failed to create one or more compute pipelines");
        return false;
    }

    // Procedural sky upload to envCubemap
    //
    // Matches the analytic sky in skybox.frag so the IBL stays consistent with
    // what the user sees in the sky. Only runs once at startup; loadHDRSkybox()
    // overwrites envCubemap when the user picks a real HDR file.
    {
        constexpr int k_cubeSz = 512;
        const size_t faceBytes = static_cast<size_t>(k_cubeSz) * k_cubeSz * 4 * sizeof(uint16_t);
        std::vector<uint16_t> faceData(static_cast<size_t>(k_cubeSz) * k_cubeSz * 4);

        auto toHalf = [](float v) -> uint16_t {
            uint32_t f;
            SDL_memcpy(&f, &v, 4);
            uint32_t sign = (f >> 16) & 0x8000;
            int32_t exp = static_cast<int32_t>((f >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = (f >> 13) & 0x03FF;
            if (exp <= 0)
                return static_cast<uint16_t>(sign);
            if (exp >= 31)
                return static_cast<uint16_t>(sign | 0x7C00);
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
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

        // Procedural sky -- mirrors skybox.frag.
        auto sky = [](glm::vec3 dir) -> glm::vec3 {
            const float y = dir.y;
            const glm::vec3 zenith(0.08f, 0.16f, 0.45f);
            const glm::vec3 horizon(0.6f, 0.45f, 0.35f);
            const glm::vec3 nadir(0.03f, 0.03f, 0.05f);
            glm::vec3 c;
            if (y > 0.0f)
                c = glm::mix(horizon, zenith, std::pow(y, 0.4f));
            else
                c = glm::mix(horizon, nadir, std::pow(-y, 0.6f));

            const glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f));
            const float sa = glm::dot(dir, sunDir);
            const float sunDisc = glm::smoothstep(0.9975f, 0.999f, sa);
            const float sunGlow = std::pow(std::max(sa, 0.0f), 256.0f);
            c += glm::vec3(1.0f, 0.95f, 0.85f) * 8.0f * sunDisc;
            c += glm::vec3(1.0f, 0.8f, 0.5f) * sunGlow * 0.5f;
            const float horizonGlow = std::exp(-std::abs(y) * 4.0f);
            c += glm::vec3(0.3f, 0.2f, 0.1f) * horizonGlow * 0.3f;
            return c;
        };

        SDL_GPUTransferBufferCreateInfo tbInfo{};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = static_cast<Uint32>(faceBytes);
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (!tb) {
            SDL_Log("IBL: failed to create transfer buffer for procedural sky");
            return false;
        }

        for (int face = 0; face < 6; ++face) {
            for (int y = 0; y < k_cubeSz; ++y) {
                for (int x = 0; x < k_cubeSz; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f) / k_cubeSz * 2.0f - 1.0f;
                    const float v = (static_cast<float>(y) + 0.5f) / k_cubeSz * 2.0f - 1.0f;
                    const glm::vec3 c = sky(cubeDir(face, u, v));
                    const size_t idx = (static_cast<size_t>(y) * k_cubeSz + x) * 4;
                    faceData[idx + 0] = toHalf(c.r);
                    faceData[idx + 1] = toHalf(c.g);
                    faceData[idx + 2] = toHalf(c.b);
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
            dst.texture = envCubemap;
            dst.layer = static_cast<Uint32>(face);
            dst.w = k_cubeSz;
            dst.h = k_cubeSz;
            dst.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, false);
            SDL_EndGPUCopyPass(cp);
            SDL_SubmitGPUCommandBuffer(cmd);
            SDL_WaitForGPUIdle(device);
        }
        SDL_ReleaseGPUTransferBuffer(device, tb);
    }

    // BRDF LUT (one-time)
    //
    // Self-contained -- no input cubemap needed. The split-sum BRDF integration
    // depends only on (NdotV, roughness).
    {
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUStorageTextureReadWriteBinding rw{};
        rw.texture = brdfLUT;
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, brdfLutPipeline);
        SDL_DispatchGPUCompute(pass, 512 / 16, 512 / 16, 1);
        SDL_EndGPUComputePass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(device);
    }

    // Irradiance + prefilter from procedural sky
    if (!regenerateIBLFromCubemap(envCubemap)) {
        SDL_Log("IBL: regenerateIBLFromCubemap failed for procedural sky");
        return false;
    }

    SDL_Log("IBL: GPU-prefiltered BRDF LUT (512), irradiance (32x6), prefilter (128x6 + 5 mips)");
    return true;
}

// Helper: dispatch irradiance.comp + prefilter.comp against `envCube`.

bool Renderer::regenerateIBLFromCubemap(SDL_GPUTexture* envCube)
{
    if (!envCube || !irradianceMap || !prefilterMap || !irradianceWorkMap || !prefilterWorkMap || !iblSampler ||
        !irradiancePipeline || !prefilterPipeline)
    {
        SDL_Log("IBL: regenerateIBLFromCubemap missing required resources");
        return false;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
        return false;

    constexpr int k_irradianceSize = 32;
    constexpr int k_prefilterBaseSize = 128;
    constexpr int k_prefilterMips = 5;

    // Irradiance: one dispatch per cube face.
    for (int face = 0; face < 6; ++face) {
        SDL_GPUStorageTextureReadWriteBinding rw{};
        rw.texture = irradianceWorkMap;
        rw.mip_level = 0;
        rw.layer = static_cast<Uint32>(face);
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, irradiancePipeline);

        SDL_GPUTextureSamplerBinding samp{};
        samp.texture = envCube;
        samp.sampler = iblSampler;
        SDL_BindGPUComputeSamplers(pass, 0, &samp, 1);

        struct
        {
            int face;
            int _p1, _p2, _p3;
        } params{face, 0, 0, 0};
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

        SDL_DispatchGPUCompute(pass, (k_irradianceSize + 15) / 16, (k_irradianceSize + 15) / 16, 1);
        SDL_EndGPUComputePass(pass);
    }

    {
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        for (int face = 0; face < 6; ++face) {
            SDL_GPUTextureLocation src{};
            src.texture = irradianceWorkMap;
            src.layer = static_cast<Uint32>(face);

            SDL_GPUTextureLocation dst{};
            dst.texture = irradianceMap;
            dst.layer = static_cast<Uint32>(face);

            SDL_CopyGPUTextureToTexture(copyPass,
                                        &src,
                                        &dst,
                                        static_cast<Uint32>(k_irradianceSize),
                                        static_cast<Uint32>(k_irradianceSize),
                                        1,
                                        false);
        }
        SDL_EndGPUCopyPass(copyPass);
    }

    // Prefilter: one dispatch per (mip, face). Mip 0 = mirror, mip 4 = roughness 1.0.
    for (int mip = 0; mip < k_prefilterMips; ++mip) {
        const int mipSize = k_prefilterBaseSize >> mip;
        const float roughness = static_cast<float>(mip) / static_cast<float>(k_prefilterMips - 1);

        for (int face = 0; face < 6; ++face) {
            SDL_GPUStorageTextureReadWriteBinding rw{};
            rw.texture = prefilterWorkMap;
            rw.mip_level = static_cast<Uint32>(mip);
            rw.layer = static_cast<Uint32>(face);
            SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
            SDL_BindGPUComputePipeline(pass, prefilterPipeline);

            SDL_GPUTextureSamplerBinding samp{};
            samp.texture = envCube;
            samp.sampler = iblSampler;
            SDL_BindGPUComputeSamplers(pass, 0, &samp, 1);

            struct
            {
                float roughness;
                int face;
                int _p1, _p2;
            } params{roughness, face, 0, 0};
            SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

            // Workgroup is 16x16 -- mips 5+ would underflow, but k_prefilterMips
            // == 5 means smallest mip = 8 which still makes one full workgroup.
            const Uint32 groups = static_cast<Uint32>(std::max(mipSize / 16, 1));
            SDL_DispatchGPUCompute(pass, groups, groups, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    {
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        for (int mip = 0; mip < k_prefilterMips; ++mip) {
            const Uint32 mipSize = static_cast<Uint32>(k_prefilterBaseSize >> mip);
            for (int face = 0; face < 6; ++face) {
                SDL_GPUTextureLocation src{};
                src.texture = prefilterWorkMap;
                src.mip_level = static_cast<Uint32>(mip);
                src.layer = static_cast<Uint32>(face);

                SDL_GPUTextureLocation dst{};
                dst.texture = prefilterMap;
                dst.mip_level = static_cast<Uint32>(mip);
                dst.layer = static_cast<Uint32>(face);

                SDL_CopyGPUTextureToTexture(copyPass, &src, &dst, mipSize, mipSize, 1, false);
            }
        }
        SDL_EndGPUCopyPass(copyPass);
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device);
    return true;
}

// HDR skybox loading (equirectangular -> cubemap + IBL regen)

void Renderer::scanHDRFiles()
{
    availableHDRFiles.clear();
    const char* base = SDL_GetBasePath();
    std::string dir = std::string(base ? base : "") + "assets/uploads_files_812442_HdriFree/";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec))
        return;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".hdr" || ext == ".exr")
                availableHDRFiles.push_back(entry.path().string());
        }
    }
    std::sort(availableHDRFiles.begin(), availableHDRFiles.end());
}

bool Renderer::loadHDRSkybox(const std::string& path)
{
    // Load equirectangular HDR
    int hdrW = 0, hdrH = 0, hdrC = 0;
    float* hdrData = stbi_loadf(path.c_str(), &hdrW, &hdrH, &hdrC, 3);
    if (!hdrData) {
        SDL_Log("HDR skybox: failed to load %s: %s", path.c_str(), stbi_failure_reason());
        return false;
    }
    SDL_Log("HDR skybox: loaded %s (%dx%d)", path.c_str(), hdrW, hdrH);

    // Convert to float16 helpers
    auto toHalf = [](float v) -> uint16_t {
        uint32_t f;
        SDL_memcpy(&f, &v, 4);
        uint32_t sign = (f >> 16) & 0x8000;
        int32_t exp = static_cast<int32_t>(((f >> 23) & 0xFF)) - 127 + 15;
        uint32_t mant = (f >> 13) & 0x03FF;
        if (exp <= 0)
            return static_cast<uint16_t>(sign);
        if (exp >= 31)
            return static_cast<uint16_t>(sign | 0x7C00);
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
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
        default:
            return glm::vec3(0);
        }
    };

    // Bilinear sample the equirectangular HDR image.
    auto sampleEquirect = [&](glm::vec3 dir) -> glm::vec3 {
        float theta = std::atan2(dir.z, dir.x);                // [-PI, PI]
        float phi = std::asin(glm::clamp(dir.y, -1.0f, 1.0f)); // [-PI/2, PI/2]
        float u = theta / (2.0f * 3.14159265f) + 0.5f;         // [0, 1]
        float v = phi / 3.14159265f + 0.5f;                    // [0, 1]
        // Bilinear sampling.
        float fx = u * static_cast<float>(hdrW) - 0.5f;
        float fy = (1.0f - v) * static_cast<float>(hdrH) - 0.5f;
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float sx = fx - static_cast<float>(x0);
        float sy = fy - static_cast<float>(y0);
        auto pixel = [&](int px, int py) -> glm::vec3 {
            px = ((px % hdrW) + hdrW) % hdrW;
            py = glm::clamp(py, 0, hdrH - 1);
            const float* p = hdrData + (static_cast<size_t>(py) * hdrW + px) * 3;
            return glm::vec3(p[0], p[1], p[2]);
        };
        return glm::mix(
            glm::mix(pixel(x0, y0), pixel(x0 + 1, y0), sx), glm::mix(pixel(x0, y0 + 1), pixel(x0 + 1, y0 + 1), sx), sy);
    };

    // Create cubemap faces (512x512, RGBA16F).
    const int cubeSz = 512;
    const size_t faceBytes = static_cast<size_t>(cubeSz * cubeSz * 4) * sizeof(uint16_t);
    std::vector<uint16_t> faceData(static_cast<size_t>(cubeSz * cubeSz * 4));

    // Release old cubemap if present.
    if (envCubemap) {
        SDL_WaitForGPUIdle(device);
        SDL_ReleaseGPUTexture(device, envCubemap);
        envCubemap = nullptr;
    }

    // Create new cubemap texture.
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_CUBE;
    ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    ci.width = static_cast<Uint32>(cubeSz);
    ci.height = static_cast<Uint32>(cubeSz);
    ci.layer_count_or_depth = 6;
    ci.num_levels = 1;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    envCubemap = SDL_CreateGPUTexture(device, &ci);
    if (!envCubemap) {
        SDL_Log("HDR skybox: failed to create cubemap: %s", SDL_GetError());
        stbi_image_free(hdrData);
        return false;
    }

    // Transfer buffer for uploading faces.
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = static_cast<Uint32>(faceBytes);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb) {
        stbi_image_free(hdrData);
        return false;
    }

    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < cubeSz; ++y) {
            for (int x = 0; x < cubeSz; ++x) {
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(cubeSz) * 2.0f - 1.0f;
                float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(cubeSz) * 2.0f - 1.0f;
                glm::vec3 dir = cubeDir(face, u, v);
                glm::vec3 color = sampleEquirect(dir);

                // Convert to RGBA16F. The IBL convolution sees this same data
                // through `envCubemap` once it's uploaded -- no CPU-side copy
                // required.
                size_t idx = (static_cast<size_t>(y) * cubeSz + x) * 4;
                faceData[idx + 0] = toHalf(color.r);
                faceData[idx + 1] = toHalf(color.g);
                faceData[idx + 2] = toHalf(color.b);
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
        dst.texture = envCubemap;
        dst.layer = static_cast<Uint32>(face);
        dst.w = static_cast<Uint32>(cubeSz);
        dst.h = static_cast<Uint32>(cubeSz);
        dst.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(device);
    }
    SDL_ReleaseGPUTransferBuffer(device, tb);
    stbi_image_free(hdrData);

    // Convolve the new environment cubemap on the GPU -> irradiance + prefilter.
    if (!regenerateIBLFromCubemap(envCubemap)) {
        SDL_Log("HDR skybox: GPU IBL regeneration failed for %s", path.c_str());
        return false;
    }

    // Update state.
    currentHDRName = std::filesystem::path(path).stem().string();
    useHDRSkybox = true;
    SDL_Log("HDR skybox: cubemap + IBL rebuilt from %s", currentHDRName.c_str());
    return true;
}

// Post-processing init (Phases 7-13)

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
    // GTAO: 1 sampler (depth), 1 rw storage tex (AO output), 1 UBO.
    ssaoPipeline = createComputePipeline("gtao.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    // Bilateral blur: 2 samplers (AO + depth), 1 rw storage tex, 1 UBO.
    ssaoBlurPipeline = createComputePipeline("gtao_blur.comp", 2, 0, 0, 1, 0, 1, 16, 16, 1);
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

bool Renderer::initSMAA()
{
    // Motion vectors compute pipeline (shared with temporal resolve).
    motionVectorPipeline = createComputePipeline("motion_vectors.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);

    // Temporal resolve compute pipeline (SMAA T2x).
    smaaResolvePipeline = createComputePipeline("smaa_resolve.comp", 3, 0, 0, 1, 0, 1, 16, 16, 1);

    // SMAA graphics pipelines (fullscreen triangle, no depth, no vertex input).
    // Each uses smaa_fullscreen.vert (0 samplers, 0 UBOs) + a fragment shader.
    auto makeSmaaGraphicsPipeline = [&](const char* fragName,
                                        Uint32 fragSamplers,
                                        Uint32 fragUBOs,
                                        SDL_GPUTextureFormat colorFmt) -> SDL_GPUGraphicsPipeline* {
        SDL_GPUShader* vert = loadShaderFromFile("smaa_fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
        SDL_GPUShader* frag = loadShaderFromFile(fragName, SDL_GPU_SHADERSTAGE_FRAGMENT, fragSamplers, fragUBOs);
        if (!vert || !frag) {
            SDL_ReleaseGPUShader(device, vert);
            SDL_ReleaseGPUShader(device, frag);
            return nullptr;
        }

        SDL_GPUColorTargetDescription ct{};
        ct.format = colorFmt;

        SDL_GPUGraphicsPipelineCreateInfo pci{};
        pci.vertex_shader = vert;
        pci.fragment_shader = frag;
        pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pci.target_info.color_target_descriptions = &ct;
        pci.target_info.num_color_targets = 1;
        pci.target_info.has_depth_stencil_target = false;
        pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        if (!pipeline)
            SDL_Log("Renderer: SMAA pipeline '%s' creation failed: %s", fragName, SDL_GetError());
        return pipeline;
    };

    smaaEdgePipeline = makeSmaaGraphicsPipeline("smaa_edge.frag", 2, 1, SDL_GPU_TEXTUREFORMAT_R8G8_UNORM);
    smaaBlendPipeline = makeSmaaGraphicsPipeline("smaa_blend.frag", 3, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    smaaNeighborhoodPipeline =
        makeSmaaGraphicsPipeline("smaa_neighborhood.frag", 3, 1, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);

    // Upload SMAA area texture (160x560, RG8_UNORM, 2 bytes/pixel).
    {
        constexpr int areaW = 160, areaH = 560;
        constexpr Uint32 areaBytes = areaW * areaH * 2;
        std::vector<unsigned char> areaData(areaBytes);
        generateAreaTex(areaData.data());

        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
        ci.width = areaW;
        ci.height = areaH;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        smaaAreaTex = SDL_CreateGPUTexture(device, &ci);

        if (smaaAreaTex) {
            SDL_GPUTransferBufferCreateInfo tbInfo{};
            tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbInfo.size = areaBytes;
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
            if (tb) {
                void* ptr = SDL_MapGPUTransferBuffer(device, tb, false);
                SDL_memcpy(ptr, areaData.data(), areaBytes);
                SDL_UnmapGPUTransferBuffer(device, tb);

                SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
                SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = tb;
                SDL_GPUTextureRegion dst{};
                dst.texture = smaaAreaTex;
                dst.w = areaW;
                dst.h = areaH;
                dst.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &dst, false);
                SDL_EndGPUCopyPass(cp);
                SDL_SubmitGPUCommandBuffer(cmd);
                SDL_WaitForGPUIdle(device);
                SDL_ReleaseGPUTransferBuffer(device, tb);
            }
        }
    }

    // Upload SMAA search texture (66x33, R8_UNORM, 1 byte/pixel).
    {
        constexpr int searchW = 66, searchH = 33;
        constexpr Uint32 searchBytes = searchW * searchH;
        std::vector<unsigned char> searchData(searchBytes);
        generateSearchTex(searchData.data());

        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        ci.width = searchW;
        ci.height = searchH;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        smaaSearchTex = SDL_CreateGPUTexture(device, &ci);

        if (smaaSearchTex) {
            SDL_GPUTransferBufferCreateInfo tbInfo{};
            tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbInfo.size = searchBytes;
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
            if (tb) {
                void* ptr = SDL_MapGPUTransferBuffer(device, tb, false);
                SDL_memcpy(ptr, searchData.data(), searchBytes);
                SDL_UnmapGPUTransferBuffer(device, tb);

                SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
                SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = tb;
                SDL_GPUTextureRegion dst{};
                dst.texture = smaaSearchTex;
                dst.w = searchW;
                dst.h = searchH;
                dst.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &dst, false);
                SDL_EndGPUCopyPass(cp);
                SDL_SubmitGPUCommandBuffer(cmd);
                SDL_WaitForGPUIdle(device);
                SDL_ReleaseGPUTransferBuffer(device, tb);
            }
        }
    }

    SDL_Log("SMAA: initialized (%s edge, %s blend, %s neighborhood, %s resolve, area=%p, search=%p)",
            smaaEdgePipeline ? "OK" : "FAIL",
            smaaBlendPipeline ? "OK" : "FAIL",
            smaaNeighborhoodPipeline ? "OK" : "FAIL",
            smaaResolvePipeline ? "OK" : "FAIL",
            static_cast<void*>(smaaAreaTex),
            static_cast<void*>(smaaSearchTex));

    return motionVectorPipeline && smaaResolvePipeline && smaaEdgePipeline && smaaBlendPipeline &&
           smaaNeighborhoodPipeline;
}

bool Renderer::initCAS()
{
    casPipeline = createComputePipeline("cas.comp", 1, 0, 0, 1, 0, 1, 16, 16, 1);
    return casPipeline != nullptr;
}

bool Renderer::initHudBlit()
{
    SDL_GPUShader* vert = loadShaderFromFile("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader* frag = loadShaderFromFile("hud_blit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription ct{};
    ct.format = swapchainFormat;
    ct.blend_state.enable_blend = true;
    ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    hudBlitPipeline_ = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!hudBlitPipeline_) {
        SDL_Log("Renderer: HUD blit pipeline failed: %s", SDL_GetError());
        return false;
    }

    // Nearest-clamp sampler for pixel-perfect blit.
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_NEAREST;
    sci.mag_filter = SDL_GPU_FILTER_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    hudSampler_ = SDL_CreateGPUSampler(device, &sci);

    return hudBlitPipeline_ && hudSampler_;
}

// init

bool Renderer::init(SDL_Window* win)
{
    window = win;

    constexpr SDL_GPUShaderFormat k_wantedFormats = SDL_GPU_SHADERFORMAT_SPIRV
#ifdef HAVE_MSL_SHADERS
                                                    | SDL_GPU_SHADERFORMAT_MSL
#endif
#ifdef HAVE_DXIL_SHADERS
                                                    | SDL_GPU_SHADERFORMAT_DXIL
#endif
        ;

    device = SDL_CreateGPUDevice(k_wantedFormats, /*debug_mode=*/true, nullptr);
    if (!device) {
        SDL_Log("Renderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Renderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device));
    SDL_Log("Renderer: available shader formats = 0x%x", SDL_GetGPUShaderFormats(device));

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("Renderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Pick the shader format that matches the selected GPU backend.  Each SDL
    // GPU backend reports exactly one supported format here:
    //   Direct3D 12 → DXIL, Metal → MSL, Vulkan → SPIR-V.
    // The CMake build emits whichever blobs are needed for this platform; the
    // matching `HAVE_*_SHADERS` define gates each branch.
    const SDL_GPUShaderFormat k_available = SDL_GetGPUShaderFormats(device);
    shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
#ifdef HAVE_DXIL_SHADERS
    if (k_available & SDL_GPU_SHADERFORMAT_DXIL)
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
    else
#endif
#ifdef HAVE_MSL_SHADERS
        if (k_available & SDL_GPU_SHADERFORMAT_MSL)
        shaderFormat = SDL_GPU_SHADERFORMAT_MSL;
    else
#endif
        if (k_available & SDL_GPU_SHADERFORMAT_SPIRV)
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    if (shaderFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format");
        return false;
    }
    SDL_Log("Renderer: selected shader format = %s",
            (shaderFormat == SDL_GPU_SHADERFORMAT_DXIL)    ? "DXIL"
            : (shaderFormat == SDL_GPU_SHADERFORMAT_MSL)   ? "MSL"
            : (shaderFormat == SDL_GPU_SHADERFORMAT_SPIRV) ? "SPIR-V"
                                                           : "unknown");

    swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device, window);

    // ImGui GPU backend.
    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device = device;
    imguiInfo.ColorTargetFormat = swapchainFormat;
    imguiInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo))
        return false;

    // Create all pipelines
    if (!initPBRPipeline())
        return false;
    if (!initSkyboxPipeline())
        return false;
    if (!initTonemapPipeline())
        return false;
    // Shadow pipeline + shadow map texture + comparison sampler.
    initShadowPipeline();
    // Skinned-character pipelines (perf Phase 1B).
    if (!initSkinnedPipelines())
        SDL_Log("Renderer: skinned pipelines unavailable — animated chars will fall back to per-entity path");
    {
        // Shadow atlas: D32_FLOAT, (2*k_shadowMapSize)² with 4 cascade viewports
        // in a 2×2 grid.  Each cascade occupies one quadrant at k_shadowMapSize².
        SDL_GPUTextureCreateInfo smInfo{};
        smInfo.type = SDL_GPU_TEXTURETYPE_2D;
        smInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        smInfo.width = k_shadowMapSize * 2;
        smInfo.height = k_shadowMapSize * 2;
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

    // Post-processing compute pipelines (Phases 7-13)
    if (!initBloom())
        SDL_Log("Renderer: bloom init failed");
    if (!initSSAO())
        SDL_Log("Renderer: SSAO init failed");
    if (!initSSR())
        SDL_Log("Renderer: SSR init failed");
    if (!initVolumetrics())
        SDL_Log("Renderer: volumetrics init failed");
    if (!initSMAA())
        SDL_Log("Renderer: SMAA init failed");
    if (!initCAS())
        SDL_Log("Renderer: CAS init failed");

    // Tonemap sampler (linear, clamp-to-edge)
    SDL_GPUSamplerCreateInfo sampInfo{};
    sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    tonemapSampler = SDL_CreateGPUSampler(device, &sampInfo);

    // Nearest sampler for depth-based compute passes (GTAO, blur).
    // Prevents interpolated depth at edges (which mixes object and sky depth).
    SDL_GPUSamplerCreateInfo nearestInfo{};
    nearestInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    nearestInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    nearestInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    nearestInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearestInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearestInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearestDepthSampler = SDL_CreateGPUSampler(device, &nearestInfo);

    // Load scene models
    // Props and character models are loaded via Game::init() → AssetRegistry,
    // which generates collision data alongside visual uploads.

    const char* const k_base = SDL_GetBasePath();

    // Camera — overridden every frame by drawFrame().
    camera = Camera();

    // Load default HDR skybox
    scanHDRFiles();
    {
        char hdrPath[512];
        SDL_snprintf(
            hdrPath, sizeof(hdrPath), "%sassets/uploads_files_812442_HdriFree/CasualDay4K.hdr", k_base ? k_base : "");
        if (!loadHDRSkybox(hdrPath))
            SDL_Log("Renderer: default HDR skybox not loaded — using procedural sky");
    }

    if (!initHudBlit())
        SDL_Log("Renderer: HUD blit init failed (non-fatal)");

    return true;
}

// Render target management

bool Renderer::ensureDepthTexture(const Uint32 w, const Uint32 h)
{
    if (depthTexture && depthWidth == w && depthHeight == h)
        return true;

    if (depthTexture)
        SDL_ReleaseGPUTexture(device, depthTexture);
    if (weaponDepthTexture)
        SDL_ReleaseGPUTexture(device, weaponDepthTexture);

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

    // Weapon-only depth buffer — the weapon pass clears & draws into this
    // so the scene depthTexture (read by SSAO/SSR) is never clobbered.
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET; // no sampler needed
    weaponDepthTexture = SDL_CreateGPUTexture(device, &info);

    depthWidth = w;
    depthHeight = h;
    return depthTexture != nullptr && weaponDepthTexture != nullptr;
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

// drawFrame -- main render loop

glm::vec3 Renderer::getSunDirection() const
{
    float azRad = glm::radians(sunAzimuth);
    float elRad = glm::radians(sunElevation);
    float cosEl = std::cos(elRad);
    return glm::normalize(glm::vec3(cosEl * std::sin(azRad), std::sin(elRad), cosEl * std::cos(azRad)));
}

// Cascaded Shadow Map computation

/// @brief Compute per-cascade light view-projection matrices.
///
/// Tightly fits the camera's sub-frustum from the light's perspective.  Uses
/// the practical split scheme (blend of logarithmic and linear) and
/// texel-snaps the ortho projection to prevent shadow swimming.
/// @param cam Current camera.
/// @param lightDir Unit direction vector toward the sun.
/// @param numCascades Number of cascades (up to 4).
/// @param shadowMapSize Per-cascade shadow map resolution.
/// @param shadowMaxDist Maximum shadow distance in world units.
/// @param lambda Logarithmic vs linear blend factor (0=linear, 1=log).
/// @return Array of CascadeInfo structs with light VP matrices and split distances.
static std::array<CascadeInfo, 4> computeCascades(
    const Camera& cam, const glm::vec3& lightDir, int numCascades, int shadowMapSize, float shadowMaxDist, float lambda)
{
    std::array<CascadeInfo, 4> cascades{};

    const float camNear = cam.getNear();

    // Compute split distances (practical split scheme)
    float splits[5];
    splits[0] = camNear;
    for (int i = 0; i < numCascades; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(numCascades);
        const float logSplit = camNear * std::pow(shadowMaxDist / camNear, p);
        const float linSplit = camNear + (shadowMaxDist - camNear) * p;
        splits[i + 1] = lambda * logSplit + (1.0f - lambda) * linSplit;
    }

    // Camera basis
    // cam.getUp() returns the raw input up (0,1,0), NOT the orthonormalized
    // up that lookAt computes.  We must derive the actual camera up from
    // forward × right to match the real frustum orientation — otherwise the
    // cascade frustum corners are wrong whenever the camera has pitch, causing
    // shadows to detach from objects and move with the camera.
    const glm::vec3 camPos = cam.getEye();
    const glm::vec3 camFwd = cam.getForward();
    const glm::vec3 camRight = cam.getRight();
    const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camFwd));

    const float fovY = glm::radians(cam.getFovy());
    const float aspect = cam.getAspect();
    const float tanHalfY = std::tan(fovY * 0.5f);
    const float tanHalfX = tanHalfY * aspect;

    // Stable up vector — avoid degenerate lookAt when light is directly vertical.
    const glm::vec3 upVec = (std::abs(lightDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    for (int c = 0; c < numCascades; ++c) {
        const float nearDist = splits[c];
        const float farDist = splits[c + 1];
        cascades[static_cast<size_t>(c)].splitDistance = farDist;

        // 8 frustum corners for this sub-frustum
        const float nH = tanHalfY * nearDist;
        const float nW = tanHalfX * nearDist;
        const float fH = tanHalfY * farDist;
        const float fW = tanHalfX * farDist;

        const glm::vec3 nc = camPos + camFwd * nearDist;
        const glm::vec3 fc = camPos + camFwd * farDist;

        const glm::vec3 corners[8] = {
            nc - camRight * nW + camUp * nH,
            nc + camRight * nW + camUp * nH,
            nc - camRight * nW - camUp * nH,
            nc + camRight * nW - camUp * nH,
            fc - camRight * fW + camUp * fH,
            fc + camRight * fW + camUp * fH,
            fc - camRight * fW - camUp * fH,
            fc + camRight * fW - camUp * fH,
        };

        // Sub-frustum center
        glm::vec3 center(0.0f);
        for (const auto& corner : corners)
            center += corner;
        center /= 8.0f;

        // Light view matrix
        // Place the eye far enough behind the center to encompass all
        // potential shadow casters (buildings, terrain behind the camera, etc.).
        constexpr float k_lightDistance = 5000.0f;
        const glm::mat4 lightView = glm::lookAt(center + lightDir * k_lightDistance, center, upVec);

        // AABB of sub-frustum in light space
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (const auto& corner : corners) {
            const glm::vec3 ls = glm::vec3(lightView * glm::vec4(corner, 1.0f));
            minX = std::min(minX, ls.x);
            maxX = std::max(maxX, ls.x);
            minY = std::min(minY, ls.y);
            maxY = std::max(maxY, ls.y);
            minZ = std::min(minZ, ls.z);
            maxZ = std::max(maxZ, ls.z);
        }

        // Texel-snap XY (prevents shadow swimming on camera movement)
        const float texelX = (maxX - minX) / static_cast<float>(shadowMapSize);
        const float texelY = (maxY - minY) / static_cast<float>(shadowMapSize);

        minX = std::floor(minX / texelX) * texelX;
        maxX = std::ceil(maxX / texelX) * texelX;
        minY = std::floor(minY / texelY) * texelY;
        maxY = std::ceil(maxY / texelY) * texelY;

        // Orthographic projection (RH, zero-to-one depth for Vulkan)
        // Near/far derived from the frustum Z AABB.  The near plane is extended
        // significantly backward to capture shadow casters (buildings, terrain)
        // behind the camera sub-frustum that still cast into it.
        // In light view space (RH), objects in front of the eye have Z < 0.
        //   near = -maxZ  (closest point to eye, smallest positive distance)
        //   far  = -minZ  (farthest point, largest positive distance)
        constexpr float k_casterPadding = 2000.0f; // catch casters behind frustum
        const float orthoNear = std::max(0.1f, -maxZ - k_casterPadding);
        const float orthoFar = -minZ + 500.0f;     // small forward padding
        const glm::mat4 lightProj = glm::orthoRH_ZO(minX, maxX, minY, maxY, orthoNear, orthoFar);

        cascades[static_cast<size_t>(c)].lightView = lightView;
        cascades[static_cast<size_t>(c)].lightProj = lightProj;
        cascades[static_cast<size_t>(c)].lightVP = lightProj * lightView;
    }

    return cascades;
}

void Renderer::drawFrame(const glm::vec3 eye, const float yaw, const float pitch, const float roll)
{
    // Per-frame phase timing (read by Game::iterate's bench profiler).
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 drawT0 = SDL_GetPerformanceCounter();
    auto deltaMs = [&](Uint64 from, Uint64 to) {
        return static_cast<float>(to - from) * 1000.0f / static_cast<float>(perfFreq);
    };

    // Camera setup. setTarget() handles the forward-vector math from
    // pitch/yaw and applies roll by tilting the up vector around forward
    // (used for wallrun camera tilt).
    camera.setEye(eye);
    camera.setTarget(pitch, yaw, roll);

    // Acquire GPU resources
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) {
        lastAcquireMs = lastRecordMs = lastSubmitMs = 0.0f;
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    const Uint64 acquireT0 = SDL_GetPerformanceCounter();
    const bool acquired = SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h);
    const Uint64 acquireT1 = SDL_GetPerformanceCounter();
    lastAcquireMs = deltaMs(acquireT0, acquireT1);
    if (!acquired || !swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        lastRecordMs = 0.0f;
        lastSubmitMs = 0.0f;
        return;
    }

    // Flush pending skinned-mesh vertex uploads
    // Batched into this frame's command buffer — one copy pass for all meshes,
    // zero extra SDL_SubmitGPUCommandBuffer calls, zero pipeline stalls.
    if (!pendingVertexUploads.empty()) {
        // Find the largest upload to size the transfer buffer.
        Uint32 maxBytes = 0;
        for (const auto& up : pendingVertexUploads)
            maxBytes = std::max(maxBytes, static_cast<Uint32>(up.data.size()));

        // Grow the persistent transfer buffer if needed.
        if (maxBytes > skinTransferBufSize) {
            if (skinTransferBuf)
                SDL_ReleaseGPUTransferBuffer(device, skinTransferBuf);
            SDL_GPUTransferBufferCreateInfo tbInfo{};
            tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbInfo.size = maxBytes;
            skinTransferBuf = SDL_CreateGPUTransferBuffer(device, &tbInfo);
            skinTransferBufSize = skinTransferBuf ? maxBytes : 0;
        }

        if (skinTransferBuf) {
            for (const auto& up : pendingVertexUploads) {
                // cycle=true: SDL allocates a fresh staging region each call so
                // the GPU can still read from the previous frame's copy without stalling.
                void* mapped = SDL_MapGPUTransferBuffer(device, skinTransferBuf, /*cycle=*/true);
                if (!mapped)
                    continue;
                SDL_memcpy(mapped, up.data.data(), up.data.size());
                SDL_UnmapGPUTransferBuffer(device, skinTransferBuf);

                SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTransferBufferLocation src{};
                src.transfer_buffer = skinTransferBuf;
                src.offset = 0;
                SDL_GPUBufferRegion dst{};
                dst.buffer = up.dstBuffer;
                dst.offset = 0;
                dst.size = static_cast<Uint32>(up.data.size());
                SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/false);
                SDL_EndGPUCopyPass(cp);
            }
        }
        pendingVertexUploads.clear();
    }

    // Upload skinned frame palette + instance SSBOs (perf Phase 1B).
    if (skinnedRigInstalled && skinnedFrameDirty) {
        SDL_GPUCopyPass* sCp = SDL_BeginGPUCopyPass(cmd);
        uploadSkinnedFrame(cmd, sCp);
        SDL_EndGPUCopyPass(sCp);
    }

    if (!ensureDepthTexture(w, h) || !ensureHDRTarget(w, h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    // Ensure post-processing textures exist at screen resolution.
    // Recreate all post-processing textures when the screen size changes.
    const bool ppResize = (postProcW != w || postProcH != h);
    if (ppResize)
        SDL_WaitForGPUIdle(device); // Drain in-flight commands before releasing textures.

    // Bloom mip chain (lazy create / resize).
    {
        Uint32 mipW = w / 2, mipH = h / 2;
        for (int i = 0; i < k_bloomMips; ++i) {
            if (!bloomMips[i] || ppResize) {
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

    // Helper: create an R8_UNORM texture.
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

    // Helper: create an RGBA16F texture.
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

    // SSAO textures — half resolution (perf Phase 3e).  The bilateral blur
    // already softens edges + the GTAO output is monochromatic, so dropping
    // to half-res is visually transparent but cuts dispatch work by 4×.
    if (!ssaoTexture || ppResize) {
        if (ssaoTexture)
            SDL_ReleaseGPUTexture(device, ssaoTexture);
        if (ssaoBlurTexture)
            SDL_ReleaseGPUTexture(device, ssaoBlurTexture);
        const Uint32 ssaoW = std::max(w / 2, 1u);
        const Uint32 ssaoH = std::max(h / 2, 1u);
        ssaoTexture = makeR8(ssaoW, ssaoH);
        ssaoBlurTexture = makeR8(ssaoW, ssaoH);
    }

    // SSR (perf Phase 3e — half-resolution).  SSR is a heavy ray-march pass
    // and its output is already noisy/temporally-accumulated; a half-res
    // SSR target is hard to distinguish from a full-res one but cuts the
    // pass's cost by 4×.  Both consumers (next-frame SSR temporal blend,
    // tonemap composite) sample with the linear sampler, so bilinear
    // upscaling is free.
    if (!ssrTexture[0] || ppResize) {
        for (auto*& t : ssrTexture) {
            if (t)
                SDL_ReleaseGPUTexture(device, t);
        }
        const Uint32 ssrW = std::max(w / 2, 1u);
        const Uint32 ssrH = std::max(h / 2, 1u);
        ssrTexture[0] = makeRGBA16F(ssrW, ssrH);
        ssrTexture[1] = makeRGBA16F(ssrW, ssrH);
    }
    if (!volumetricTexture || ppResize) {
        if (volumetricTexture)
            SDL_ReleaseGPUTexture(device, volumetricTexture);
        volumetricTexture = makeRGBA16F(w / 2, h / 2);
    }
    if (!taaHistory[0] || ppResize) {
        for (auto*& t : taaHistory) {
            if (t)
                SDL_ReleaseGPUTexture(device, t);
        }
        taaHistory[0] = makeRGBA16F(w, h);
        taaHistory[1] = makeRGBA16F(w, h);
    }
    if (!motionVectorTexture || ppResize) {
        if (motionVectorTexture)
            SDL_ReleaseGPUTexture(device, motionVectorTexture);
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

    // SMAA textures (recreate on resize, same as other post-processing textures).
    if (!smaaEdgeTex || ppResize) {
        if (smaaEdgeTex)
            SDL_ReleaseGPUTexture(device, smaaEdgeTex);
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.width = w;
        ci.height = h;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        smaaEdgeTex = SDL_CreateGPUTexture(device, &ci);
    }
    if (!smaaBlendTex || ppResize) {
        if (smaaBlendTex)
            SDL_ReleaseGPUTexture(device, smaaBlendTex);
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.width = w;
        ci.height = h;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        smaaBlendTex = SDL_CreateGPUTexture(device, &ci);
    }
    if (!smaaOutputTex || ppResize) {
        if (smaaOutputTex)
            SDL_ReleaseGPUTexture(device, smaaOutputTex);
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.width = w;
        ci.height = h;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        smaaOutputTex = SDL_CreateGPUTexture(device, &ci);
    }
    if (!casOutputTex || ppResize) {
        if (casOutputTex)
            SDL_ReleaseGPUTexture(device, casOutputTex);
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.width = w;
        ci.height = h;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        casOutputTex = SDL_CreateGPUTexture(device, &ci);
    }

    postProcW = w;
    postProcH = h;

    camera.setAspect(static_cast<float>(w), static_cast<float>(h));

    // Store unjittered matrices BEFORE applying jitter.
    // Motion vectors + GTAO need the stable (unjittered) projection.
    const glm::mat4 unjitteredVP = camera.getViewProjection();
    const glm::mat4 unjitteredProj = camera.getProjectionMatrix();

    if (aaMode >= AAMode::SMAA_T2x) {
        const float jitterPixels = (frameIndex == 0) ? -0.25f : 0.25f;
        const float jx = jitterPixels * 2.0f / static_cast<float>(w);
        const float jy = jitterPixels * 2.0f / static_cast<float>(h);
        camera.applySubpixelJitter(jx, jy);
    }

    // Upload particle data (BEFORE any render pass)
    if (particleSystem && toggles.particles)
        particleSystem->uploadToGpu(cmd);

    // Prepare ImGui (skip entirely when imguiEnabled = false; the bench flips
    // this off so the entire ImGui CPU prepare + GPU render is bypassed).
    ImDrawData* const drawData = imguiEnabled ? ImGui::GetDrawData() : nullptr;
    if (drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);

    // PASS 0: Cascaded Shadow Maps -- 4 depth-only passes, one per cascade
    std::array<CascadeInfo, 4> cascades{};
    if (toggles.shadows && shadowPipeline && shadowMap) {
        cascades = computeCascades(
            camera, getSunDirection(), k_shadowCascades, k_shadowMapSize, shadowDistance, cascadeLambda);

        // One render pass for the whole atlas — clear to depth=1.0, then
        // viewport/scissor per cascade quadrant.
        SDL_GPUDepthStencilTargetInfo sdt{};
        sdt.texture = shadowMap;
        sdt.clear_depth = 1.0f;
        sdt.load_op = SDL_GPU_LOADOP_CLEAR;
        sdt.store_op = SDL_GPU_STOREOP_STORE;
        sdt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        sdt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* shadowPass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &sdt);

        for (int c = 0; c < k_shadowCascades; ++c) {
            const auto& cascade = cascades[static_cast<size_t>(c)];

            // Viewport + scissor for this cascade's atlas quadrant
            const auto vx = static_cast<float>((c % 2) * k_shadowMapSize);
            const auto vy = static_cast<float>((c / 2) * k_shadowMapSize);
            const auto vs = static_cast<float>(k_shadowMapSize);
            const SDL_GPUViewport viewport = {vx, vy, vs, vs, 0.0f, 1.0f};
            SDL_SetGPUViewport(shadowPass, &viewport);
            const SDL_Rect scissor = {
                (c % 2) * k_shadowMapSize, (c / 2) * k_shadowMapSize, k_shadowMapSize, k_shadowMapSize};
            SDL_SetGPUScissor(shadowPass, &scissor);

            // Scene models (Assimp-loaded)
            SDL_BindGPUGraphicsPipeline(shadowPass, shadowPipeline);
            for (const auto& model : models) {
                if (!model.drawInScenePass)
                    continue;
                ShadowUBO shadowUBO{};
                shadowUBO.lightVP = cascade.lightVP;
                shadowUBO.model = model.transform;
                SDL_PushGPUVertexUniformData(cmd, 0, &shadowUBO, sizeof(shadowUBO));

                for (const auto& mesh : model.meshes) {
                    if (mesh.isTransparent)
                        continue;
                    const SDL_GPUBufferBinding vbBind = {.buffer = mesh.vertexBuffer, .offset = 0};
                    SDL_BindGPUVertexBuffers(shadowPass, 0, &vbBind, 1);
                    const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
                    SDL_BindGPUIndexBuffer(shadowPass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    SDL_DrawGPUIndexedPrimitives(shadowPass, mesh.indexCount, 1, 0, 0, 0);
                }
            }

            // Entity models (ECS-driven -- now cast shadows)
            for (const auto& ecmd : entityRenderCmds) {
                if (ecmd.modelIndex < 0 || ecmd.modelIndex >= static_cast<int>(models.size()))
                    continue;
                const auto& emodel = models[static_cast<size_t>(ecmd.modelIndex)];
                ShadowUBO shadowUBO{};
                shadowUBO.lightVP = cascade.lightVP;
                shadowUBO.model = ecmd.worldTransform;
                SDL_PushGPUVertexUniformData(cmd, 0, &shadowUBO, sizeof(shadowUBO));

                for (const auto& mesh : emodel.meshes) {
                    if (mesh.isTransparent)
                        continue;
                    const SDL_GPUBufferBinding vbBind = {.buffer = mesh.vertexBuffer, .offset = 0};
                    SDL_BindGPUVertexBuffers(shadowPass, 0, &vbBind, 1);
                    const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
                    SDL_BindGPUIndexBuffer(shadowPass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    SDL_DrawGPUIndexedPrimitives(shadowPass, mesh.indexCount, 1, 0, 0, 0);
                }
            }

            // Skinned characters — single instanced draw per mesh (perf Phase 1B).
            //
            // Phase 3 — skinned chars only contribute to the near cascades
            // (0 + 1).  Cascades 2 + 3 cover distant terrain where character-
            // sized shadow detail is invisible anyway, and skipping them
            // halves the skinned-shadow vertex shader workload.
            if (c < 2 && skinnedRigInstalled && shadowSkinnedPipeline && !skinnedFrameInstances.empty() &&
                skinnedPaletteSSBO && skinnedInstanceSSBO)
            {
                SDL_BindGPUGraphicsPipeline(shadowPass, shadowSkinnedPipeline);
                ShadowUBO shadowUBO{};
                shadowUBO.lightVP = cascade.lightVP;
                shadowUBO.model = glm::mat4(1.0f); // unused on the skinned path
                SDL_PushGPUVertexUniformData(cmd, 0, &shadowUBO, sizeof(shadowUBO));

                SDL_GPUBuffer* ssbos[2] = {skinnedPaletteSSBO, skinnedInstanceSSBO};
                SDL_BindGPUVertexStorageBuffers(shadowPass, 0, ssbos, 2);

                const Uint32 numInstances = static_cast<Uint32>(skinnedFrameInstances.size());
                for (const auto& mesh : skinnedMeshes) {
                    if (!mesh.vertexBuffer || !mesh.indexBuffer || !mesh.boneBuffer)
                        continue;
                    const SDL_GPUBufferBinding vbBinds[2] = {
                        {.buffer = mesh.vertexBuffer, .offset = 0},
                        {.buffer = mesh.boneBuffer, .offset = 0},
                    };
                    SDL_BindGPUVertexBuffers(shadowPass, 0, vbBinds, 2);
                    const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
                    SDL_BindGPUIndexBuffer(shadowPass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    SDL_DrawGPUIndexedPrimitives(shadowPass, mesh.indexCount, numInstances, 0, 0, 0);
                }
            }
        }

        SDL_EndGPURenderPass(shadowPass);
    }

    // PASS 1: Main colour pass -> HDR render target
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

        // PBR models (two-pass: opaques first, then transparents)
        if (pbrSampler && !models.empty()) {
            // Light data shared by both passes.
            LightDataUBO lightData{};
            lightData.cameraPos = glm::vec4(eye, 1.0f);
            lightData.ambientColor = glm::vec4(ambientR, ambientG, ambientB, 1.0f);
            lightData.numLights = 2;
            lightData.iblDiffuseIntensity = iblDiffuseIntensity;
            lightData.iblSpecularIntensity = iblSpecularIntensity;
            const glm::vec3 sunDir = getSunDirection();
            lightData.lights[0].position = glm::vec4(sunDir, 0.0f);
            lightData.lights[0].color = glm::vec4(1.0f, 0.95f, 0.85f, sunIntensity);
            lightData.lights[1].position = glm::vec4(-sunDir, 0.0f);
            lightData.lights[1].color = glm::vec4(0.25f, 0.30f, 0.45f, fillIntensity);

            // Inject dynamic point lights (up to 6 — slots 2..7).
            for (size_t pi = 0; pi < pointLights.size() && lightData.numLights < 16; ++pi) {
                const auto& pl = pointLights[pi];
                auto& slot = lightData.lights[lightData.numLights];
                slot.position = glm::vec4(pl.position, 1.0f); // w=1 → point light
                slot.color = glm::vec4(pl.color, pl.intensity);
                slot.params = glm::vec4(pl.range, 0.0f, 0.0f, 0.0f);
                ++lightData.numLights;
            }

            // Helper: draw all scene-placed meshes matching the transparency filter.
            // Entity/weapon models (drawInScenePass==false) are handled separately.
            auto drawMeshes = [&](bool wantTransparent) {
                for (const auto& model : models) {
                    if (!model.drawInScenePass)
                        continue;
                    Matrices modelMats{};
                    modelMats.model = model.transform;
                    modelMats.view = camera.getViewMatrix();
                    modelMats.projection = camera.getProjectionMatrix();
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

            // Cascade shadow data UBO (shared by all PBR meshes).
            ShadowDataFragUBO shadowData{};
            for (int ci = 0; ci < k_shadowCascades; ++ci)
                shadowData.lightVP[ci] = cascades[static_cast<size_t>(ci)].lightVP;
            shadowData.cascadeSplits = glm::vec4(cascades[0].splitDistance,
                                                 cascades[1].splitDistance,
                                                 cascades[2].splitDistance,
                                                 cascades[3].splitDistance);
            shadowData.cameraView = camera.getViewMatrix();
            shadowData.shadowBias = shadowBiasVal;
            shadowData.shadowNormalBias = shadowNormalBiasVal;
            shadowData.lightDirWorld = glm::vec4(getSunDirection(), 0.0f);
            shadowData.lightColor = glm::vec4(1.0f, 0.95f, 0.85f, sunIntensity);
            shadowData.ambientColor = glm::vec4(ambientR, ambientG, ambientB, 1.0f);
            shadowData.fillColor = glm::vec4(0.25f, 0.30f, 0.45f, fillIntensity);
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
                glm::mat4 viewRot = camera.getViewMatrix();
                viewRot[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                skyMats.viewRotation = viewRot;
                skyMats.projection = camera.getProjectionMatrix();
                SDL_PushGPUVertexUniformData(cmd, 0, &skyMats, sizeof(skyMats));

                // Skybox fragment UBO: cubemap vs procedural selection + sun position.
                struct
                {
                    int useCubemap;
                    float envExposure;
                    float _p1, _p2;
                    glm::vec4 sunDirSky;
                } skyParams{};
                skyParams.useCubemap = (useHDRSkybox && envCubemap) ? 1 : 0;
                skyParams.envExposure = 1.0f;
                skyParams.sunDirSky = glm::vec4(getSunDirection(), 0.0f);
                SDL_PushGPUFragmentUniformData(cmd, 0, &skyParams, sizeof(skyParams));

                // Bind environment cubemap (or fallback black cubemap).
                SDL_GPUTextureSamplerBinding envSamp = {
                    .texture = (useHDRSkybox && envCubemap) ? envCubemap : irradianceMap,
                    .sampler = iblSampler ? iblSampler : tonemapSampler,
                };
                SDL_BindGPUFragmentSamplers(pass, 0, &envSamp, 1);

                SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
            }

            // Entity render commands (PBR models at entity positions)
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
                    entMats.view = camera.getViewMatrix();
                    entMats.projection = camera.getProjectionMatrix();
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

            // Skinned characters — instanced PBR draw (perf Phase 1B).
            // Single draw per mesh covers every visible animated character; the
            // vertex shader resolves per-instance bone palette + world transform
            // from SSBOs.  Replaces the per-entity model loop above for
            // animated chars (Game.cpp filters them out of entityRenderCmds).
            if (toggles.entityModels && skinnedRigInstalled && pbrSkinnedPipeline && !skinnedFrameInstances.empty() &&
                skinnedPaletteSSBO && skinnedInstanceSSBO)
            {
                SDL_BindGPUGraphicsPipeline(pass, pbrSkinnedPipeline);
                SDL_PushGPUFragmentUniformData(cmd, 1, &lightData, sizeof(lightData));
                SDL_PushGPUFragmentUniformData(cmd, 2, &shadowData, sizeof(shadowData));

                // View/proj — model matrix is unused on the skinned path but the
                // shader still declares the same Matrices block as pbr.vert.
                Matrices skMats{};
                skMats.model = glm::mat4(1.0f);
                skMats.view = camera.getViewMatrix();
                skMats.projection = camera.getProjectionMatrix();
                skMats.normalMatrix = glm::mat4(1.0f);
                SDL_PushGPUVertexUniformData(cmd, 0, &skMats, sizeof(skMats));

                SDL_GPUBuffer* ssbos[2] = {skinnedPaletteSSBO, skinnedInstanceSSBO};
                SDL_BindGPUVertexStorageBuffers(pass, 0, ssbos, 2);

                const Uint32 numInstances = static_cast<Uint32>(skinnedFrameInstances.size());
                for (const auto& mesh : skinnedMeshes) {
                    if (!mesh.vertexBuffer || !mesh.indexBuffer || !mesh.boneBuffer)
                        continue;

                    MaterialUBO matUBO{};
                    matUBO.baseColorFactor = mesh.material.baseColorFactor;
                    matUBO.metallicFactor = mesh.material.metallicFactor;
                    matUBO.roughnessFactor = mesh.material.roughnessFactor;
                    matUBO.aoStrength = mesh.material.aoStrength;
                    matUBO.normalScale = mesh.material.normalScale;
                    matUBO.emissiveFactor = mesh.material.emissiveFactor;
                    SDL_PushGPUFragmentUniformData(cmd, 0, &matUBO, sizeof(matUBO));

                    auto resolveTex = [&](int idx, SDL_GPUTexture* fallback) -> SDL_GPUTexture* {
                        if (idx >= 0 && static_cast<size_t>(idx) < skinnedTextures.size() &&
                            skinnedTextures[static_cast<size_t>(idx)])
                            return skinnedTextures[static_cast<size_t>(idx)];
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

                    const SDL_GPUBufferBinding vbBinds[2] = {
                        {.buffer = mesh.vertexBuffer, .offset = 0},
                        {.buffer = mesh.boneBuffer, .offset = 0},
                    };
                    SDL_BindGPUVertexBuffers(pass, 0, vbBinds, 2);
                    const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
                    SDL_BindGPUIndexBuffer(pass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, numInstances, 0, 0, 0);
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

        // Particle rendering (inside HDR pass, after opaques + skybox)
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
            pu.view = camera.getViewMatrix();
            pu.proj = camera.getProjectionMatrix();
            pu.camPos = camera.getEye();
            pu.camRight = camera.getRight();
            pu.camUp = camera.getUp();
            SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));

            particleSystem->setScreenSize(static_cast<float>(w), static_cast<float>(h));
            particleSystem->render(pass, cmd);
        }

        SDL_EndGPURenderPass(pass);
    }

    // Compute passes: SSAO, Bloom, SSR, Volumetrics (between HDR and tonemap)

    // GTAO (Phase 7) -- Ground Truth Ambient Occlusion
    // Always dispatch compute passes when pipeline/textures exist — toggling
    // is handled in the tonemap compositing strength.  Skipping entire compute
    // passes on the Metal backend breaks resource-transition tracking between
    // encoders, causing GPU faults when the set of active passes changes.
    if (ssaoPipeline && ssaoBlurPipeline && ssaoTexture && ssaoBlurTexture && depthTexture) {
        // GTAO main pass → ssaoTexture (raw AO).
        struct
        {
            glm::mat4 proj;
            glm::mat4 invProj;
            glm::vec2 screenSize;
            float radius;
            float falloffExp;
            int numSlices;
            int numSteps;
            float _p1, _p2;
        } gtaoUBO{};
        // Half-resolution AO targets (perf Phase 3e).  The shader still uses
        // full-res screenSize to reconstruct view-space positions correctly;
        // only the dispatch grid + storage texture are halved.
        const Uint32 aoW = std::max(w / 2, 1u);
        const Uint32 aoH = std::max(h / 2, 1u);
        // Use the unjittered projection so AO is stable across jittered frames.
        gtaoUBO.proj = unjitteredProj;
        gtaoUBO.invProj = glm::inverse(unjitteredProj);
        gtaoUBO.screenSize = glm::vec2(static_cast<float>(aoW), static_cast<float>(aoH));
        gtaoUBO.radius = ssaoRadius;
        gtaoUBO.falloffExp = ssaoFalloff;
        // Phase 3: trimmed slice/step counts.  Quality difference is barely
        // perceptible on character + scene geometry but the ALU work drops by
        // ~55% (was 3*6=18, now 2*4=8 march steps per pixel).
        gtaoUBO.numSlices = 2;
        gtaoUBO.numSteps = 4;

        SDL_GPUStorageTextureReadWriteBinding aoWrite = {.texture = ssaoTexture, .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* aoPass = SDL_BeginGPUComputePass(cmd, &aoWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(aoPass, ssaoPipeline);
        SDL_GPUTextureSamplerBinding depthSamp = {.texture = depthTexture, .sampler = nearestDepthSampler};
        SDL_BindGPUComputeSamplers(aoPass, 0, &depthSamp, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &gtaoUBO, sizeof(gtaoUBO));
        SDL_DispatchGPUCompute(aoPass, (aoW + 15) / 16, (aoH + 15) / 16, 1);
        SDL_EndGPUComputePass(aoPass);

        // Bilateral blur pass → ssaoBlurTexture (clean AO).
        struct
        {
            glm::vec2 screenSize;
            float depthSigma;
            float projA;
            float projB;
            float _p1, _p2, _p3;
        } blurUBO{};
        blurUBO.screenSize = glm::vec2(static_cast<float>(aoW), static_cast<float>(aoH));
        blurUBO.depthSigma = 0.005f;
        blurUBO.projA = unjitteredProj[2][2];
        blurUBO.projB = unjitteredProj[3][2];

        SDL_GPUStorageTextureReadWriteBinding blurWrite = {.texture = ssaoBlurTexture, .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* blurPass = SDL_BeginGPUComputePass(cmd, &blurWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(blurPass, ssaoBlurPipeline);
        SDL_GPUTextureSamplerBinding blurSamplers[2] = {
            {.texture = ssaoTexture, .sampler = tonemapSampler},
            {.texture = depthTexture, .sampler = nearestDepthSampler},
        };
        SDL_BindGPUComputeSamplers(blurPass, 0, blurSamplers, 2);
        SDL_PushGPUComputeUniformData(cmd, 0, &blurUBO, sizeof(blurUBO));
        SDL_DispatchGPUCompute(blurPass, (aoW + 15) / 16, (aoH + 15) / 16, 1);
        SDL_EndGPUComputePass(blurPass);
    }

    // Bloom (Phase 8)
    if (bloomDownsamplePipeline && bloomUpsamplePipeline && bloomMips[0]) {
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
            params.intensity = 0.3f;

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

    // SSR (Phase 9)
    static uint64_t ssrFrameCounter = 0;
    ++ssrFrameCounter;

    if (ssrPipeline && ssrTexture[0] && depthTexture && hdrTarget) {
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
        // Half-resolution dispatch (matches SSR target size from ensure-textures).
        const Uint32 ssrW = std::max(w / 2, 1u);
        const Uint32 ssrH = std::max(h / 2, 1u);
        ssrUBO.proj = camera.getProjectionMatrix();
        ssrUBO.invProj = glm::inverse(camera.getProjectionMatrix());
        ssrUBO.view = camera.getViewMatrix();
        ssrUBO.screenSize = glm::vec2(static_cast<float>(ssrW), static_cast<float>(ssrH));
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
        SDL_DispatchGPUCompute(ssrPass, (ssrW + 15) / 16, (ssrH + 15) / 16, 1);
        SDL_EndGPUComputePass(ssrPass);
        ssrCurrentIdx = ssrDst;
    }

    // Volumetrics (Phase 10)
    // Volumetric is only read by tonemap which already gates on toggles.volumetrics
    // (using fallbackBlack when off).  Skipping the compute when the toggle is off
    // is safe — the texture keeps its previous content but no consumer reads it
    // this frame.  The earlier crash from skipping was triggered by GTAO/bloom/
    // SSR (whose textures *are* read by other passes); volumetric is the cleanly
    // gateable one.
    if (toggles.volumetrics && volumetricPipeline && volumetricTexture && depthTexture && shadowMap && shadowSampler) {
        struct
        {
            glm::mat4 invViewProj;
            glm::mat4 lightVP_vol[4];    ///< Per-cascade light VP.
            glm::mat4 cameraView_vol;    ///< For view-space Z.
            glm::vec4 cascadeSplits_vol; ///< Cascade far distances.
            glm::vec4 lightDir_vol;
            glm::vec4 lightColor_vol;
            glm::vec2 screenSize;
            float fogDensity;
            float scatteringG;
            float shadowBias_vol;
            float shadowMapSize_vol; ///< Per-cascade resolution.
            float maxDistance;
            float _p2;
        } volUBO{};
        volUBO.invViewProj = glm::inverse(camera.getViewProjection());
        for (int ci = 0; ci < k_shadowCascades; ++ci)
            volUBO.lightVP_vol[ci] = cascades[static_cast<size_t>(ci)].lightVP;
        volUBO.cameraView_vol = camera.getViewMatrix();
        volUBO.cascadeSplits_vol = glm::vec4(
            cascades[0].splitDistance, cascades[1].splitDistance, cascades[2].splitDistance, cascades[3].splitDistance);
        volUBO.lightDir_vol = glm::vec4(getSunDirection(), 0.0f);
        volUBO.lightColor_vol = glm::vec4(1.0f, 0.95f, 0.85f, 2.0f);
        volUBO.screenSize = glm::vec2(static_cast<float>(w), static_cast<float>(h));
        volUBO.fogDensity = 0.0002f;
        volUBO.scatteringG = 0.7f;
        volUBO.shadowBias_vol = 0.002f;
        volUBO.shadowMapSize_vol = static_cast<float>(k_shadowMapSize);
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

    // First-person weapon viewmodel — rendered AFTER SSAO/bloom/SSR/volumetrics
    // (so those screen-space effects don't bleed through the weapon) but BEFORE
    // SMAA/TAA (so the weapon is included in the anti-aliased + tonemapped output).
    if (toggles.weaponViewmodel && weaponVM.visible && weaponVM.modelIndex >= 0 &&
        weaponVM.modelIndex < static_cast<int>(models.size()) && pbrPipeline)
    {
        SDL_GPUColorTargetInfo weapCt{};
        weapCt.texture = hdrTarget;
        weapCt.load_op = SDL_GPU_LOADOP_LOAD;
        weapCt.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo weapDt{};
        weapDt.texture = weaponDepthTexture;
        weapDt.clear_depth = 1.0f;
        weapDt.load_op = SDL_GPU_LOADOP_CLEAR;
        weapDt.store_op = SDL_GPU_STOREOP_DONT_CARE;
        weapDt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        weapDt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* weapPass = SDL_BeginGPURenderPass(cmd, &weapCt, 1, &weapDt);

        SDL_BindGPUGraphicsPipeline(weapPass, pbrPipeline);

        const auto& wmodel = models[static_cast<size_t>(weaponVM.modelIndex)];

        Matrices vmMats{};
        vmMats.model = weaponVM.transform;
        vmMats.view = camera.getViewMatrix();
        vmMats.projection = camera.getProjectionMatrix();
        vmMats.normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(weaponVM.transform)));
        SDL_PushGPUVertexUniformData(cmd, 0, &vmMats, sizeof(vmMats));

        LightDataUBO weaponLightData{};
        weaponLightData.cameraPos = glm::vec4(eye, 1.0f);
        weaponLightData.ambientColor = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
        weaponLightData.numLights = 2;
        // Suppress IBL specular on the viewmodel — it mirrors the sky cubemap,
        // making the weapon look transparent (horizon line visible on surface).
        // Keep a little diffuse IBL for ambient fill.
        weaponLightData.iblDiffuseIntensity = iblDiffuseIntensity * 0.3f;
        weaponLightData.iblSpecularIntensity = 0.0f;
        weaponLightData.lights[0].position = glm::vec4(getSunDirection(), 0.0f);
        weaponLightData.lights[0].color = glm::vec4(1.0f, 0.95f, 0.85f, sunIntensity);
        weaponLightData.lights[1].position = glm::vec4(glm::normalize(glm::vec3(-0.5f, 0.3f, -0.8f)), 0.0f);
        weaponLightData.lights[1].color = glm::vec4(0.3f, 0.4f, 0.6f, 1.0f);
        SDL_PushGPUFragmentUniformData(cmd, 1, &weaponLightData, sizeof(weaponLightData));

        ShadowDataFragUBO weaponShadow{};
        weaponShadow.shadowMapSize = 0.0f;
        SDL_PushGPUFragmentUniformData(cmd, 2, &weaponShadow, sizeof(weaponShadow));

        for (const auto& mesh : wmodel.meshes) {
            MaterialUBO matUBO{};
            matUBO.baseColorFactor = mesh.material.baseColorFactor;
            matUBO.baseColorFactor.a = 0.0f; // weapon mask: alpha=0 tells tonemap to skip post-FX
            matUBO.metallicFactor = mesh.material.metallicFactor;
            matUBO.roughnessFactor = mesh.material.roughnessFactor;
            matUBO.aoStrength = mesh.material.aoStrength;
            matUBO.normalScale = mesh.material.normalScale;
            matUBO.emissiveFactor = mesh.material.emissiveFactor;
            SDL_PushGPUFragmentUniformData(cmd, 0, &matUBO, sizeof(matUBO));

            const SDL_GPUBufferBinding vbBind = {.buffer = mesh.vertexBuffer, .offset = 0};
            SDL_BindGPUVertexBuffers(weapPass, 0, &vbBind, 1);
            const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
            SDL_BindGPUIndexBuffer(weapPass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

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
            SDL_BindGPUFragmentSamplers(weapPass, 0, samplers, 8);

            SDL_DrawGPUIndexedPrimitives(weapPass, mesh.indexCount, 1, 0, 0, 0);
        }

        SDL_EndGPURenderPass(weapPass);
    }

    // SMAA + Temporal Resolve (Phase 11) -- replaces old TAA

    // Step 1: Motion vectors (only for temporal modes).
    if (aaMode >= AAMode::SMAA_T2x && motionVectorPipeline && motionVectorTexture && depthTexture) {
        struct
        {
            glm::mat4 curInvVP;
            glm::mat4 prevVP;
            glm::vec2 screenSize;
            glm::vec2 jitter;
        } mvUBO{};
        mvUBO.curInvVP = glm::inverse(unjitteredVP);
        mvUBO.prevVP = previousVP;
        mvUBO.screenSize = glm::vec2(static_cast<float>(w), static_cast<float>(h));
        {
            const float jp = (frameIndex == 0) ? -0.25f : 0.25f;
            mvUBO.jitter = glm::vec2(jp / static_cast<float>(w), jp / static_cast<float>(h));
        }

        SDL_GPUStorageTextureReadWriteBinding mvWrite = {.texture = motionVectorTexture, .mip_level = 0, .layer = 0};
        SDL_GPUComputePass* mvPass = SDL_BeginGPUComputePass(cmd, &mvWrite, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(mvPass, motionVectorPipeline);
        SDL_GPUTextureSamplerBinding mvSamp = {.texture = depthTexture, .sampler = tonemapSampler};
        SDL_BindGPUComputeSamplers(mvPass, 0, &mvSamp, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &mvUBO, sizeof(mvUBO));
        SDL_DispatchGPUCompute(mvPass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(mvPass);
    }

    // Step 2: SMAA passes (spatial AA, all modes >= SMAA_1x).
    if (aaMode >= AAMode::SMAA_1x && smaaEdgePipeline && smaaBlendPipeline && smaaNeighborhoodPipeline && smaaEdgeTex &&
        smaaBlendTex && smaaOutputTex && smaaAreaTex && smaaSearchTex)
    {
        // Resolve SSAO texture for SMAA passes (bake AO into edges + neighborhood output).
        SDL_GPUTexture* ssaoForSmaa = (toggles.ssao && ssaoBlurTexture) ? ssaoBlurTexture : fallbackWhite;
        const float ssaoForSmaaStr = (toggles.ssao && ssaoBlurTexture) ? ssaoStr : 0.0f;

        // Pass A -- Edge Detection.
        {
            SDL_GPUColorTargetInfo ect{};
            ect.texture = smaaEdgeTex;
            ect.load_op = SDL_GPU_LOADOP_CLEAR;
            ect.clear_color = {0, 0, 0, 0};
            ect.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* epass = SDL_BeginGPURenderPass(cmd, &ect, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(epass, smaaEdgePipeline);
            SDL_GPUTextureSamplerBinding esamps[2] = {
                {.texture = hdrTarget, .sampler = tonemapSampler},
                {.texture = ssaoForSmaa, .sampler = tonemapSampler},
            };
            SDL_BindGPUFragmentSamplers(epass, 0, esamps, 2);
            struct
            {
                float w, h, threshold, aoStr;
                float aoPower, _p1, _p2, _p3;
            } edgeUBO = {(float)w, (float)h, 0.1f, ssaoForSmaaStr, ssaoPower, 0, 0, 0};
            SDL_PushGPUFragmentUniformData(cmd, 0, &edgeUBO, sizeof(edgeUBO));
            SDL_DrawGPUPrimitives(epass, 3, 1, 0, 0);
            SDL_EndGPURenderPass(epass);
        }

        // Pass B -- Blend Weights.
        {
            SDL_GPUColorTargetInfo bct{};
            bct.texture = smaaBlendTex;
            bct.load_op = SDL_GPU_LOADOP_CLEAR;
            bct.clear_color = {0, 0, 0, 0};
            bct.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* bpass = SDL_BeginGPURenderPass(cmd, &bct, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(bpass, smaaBlendPipeline);
            SDL_GPUTextureSamplerBinding bsamps[3] = {
                {.texture = smaaEdgeTex, .sampler = tonemapSampler},
                {.texture = smaaAreaTex, .sampler = tonemapSampler},
                {.texture = smaaSearchTex, .sampler = tonemapSampler},
            };
            SDL_BindGPUFragmentSamplers(bpass, 0, bsamps, 3);
            float subPixelIdx = (aaMode >= AAMode::SMAA_T2x) ? static_cast<float>(frameIndex) : 0.0f;
            struct
            {
                float w, h, subPixel, pad;
            } blendUBO = {(float)w, (float)h, subPixelIdx, 0.0f};
            SDL_PushGPUFragmentUniformData(cmd, 0, &blendUBO, sizeof(blendUBO));
            SDL_DrawGPUPrimitives(bpass, 3, 1, 0, 0);
            SDL_EndGPURenderPass(bpass);
        }

        // Pass C -- Neighborhood Blend (with SSAO baked into output).
        {
            SDL_GPUColorTargetInfo nct{};
            nct.texture = smaaOutputTex;
            nct.load_op = SDL_GPU_LOADOP_DONT_CARE;
            nct.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* npass = SDL_BeginGPURenderPass(cmd, &nct, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(npass, smaaNeighborhoodPipeline);
            SDL_GPUTextureSamplerBinding nsamps[3] = {
                {.texture = hdrTarget, .sampler = tonemapSampler},
                {.texture = smaaBlendTex, .sampler = tonemapSampler},
                {.texture = ssaoForSmaa, .sampler = tonemapSampler},
            };
            SDL_BindGPUFragmentSamplers(npass, 0, nsamps, 3);
            struct
            {
                float w, h, aoStr, aoPower;
            } nUBO = {(float)w, (float)h, ssaoForSmaaStr, ssaoPower};
            SDL_PushGPUFragmentUniformData(cmd, 0, &nUBO, sizeof(nUBO));
            SDL_DrawGPUPrimitives(npass, 3, 1, 0, 0);
            SDL_EndGPURenderPass(npass);
        }
    }

    // Step 3: Temporal resolve (T2x only).
    if (aaMode >= AAMode::SMAA_T2x && smaaResolvePipeline && taaHistory[0] && taaHistory[1] && motionVectorTexture &&
        smaaOutputTex)
    {
        const int srcIdx = taaCurrentIdx;
        const int dstIdx = 1 - taaCurrentIdx;
        struct
        {
            glm::vec2 screenSize;
            float blendFactor;
            float _p;
        } resolveUBO;
        resolveUBO.screenSize = glm::vec2((float)w, (float)h);
        resolveUBO.blendFactor = 0.2f;

        SDL_GPUStorageTextureReadWriteBinding rw = {.texture = taaHistory[dstIdx]};
        SDL_GPUComputePass* rpass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(rpass, smaaResolvePipeline);
        SDL_GPUTextureSamplerBinding rsamps[3] = {
            {.texture = smaaOutputTex, .sampler = tonemapSampler},
            {.texture = taaHistory[srcIdx], .sampler = tonemapSampler},
            {.texture = motionVectorTexture, .sampler = tonemapSampler},
        };
        SDL_BindGPUComputeSamplers(rpass, 0, rsamps, 3);
        SDL_PushGPUComputeUniformData(cmd, 0, &resolveUBO, sizeof(resolveUBO));
        SDL_DispatchGPUCompute(rpass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(rpass);

        taaCurrentIdx = dstIdx;
        previousVP = unjitteredVP;
        frameIndex = 1 - frameIndex;
    }

    // Step 4: Determine AA source texture and apply CAS sharpening.
    SDL_GPUTexture* aaSource = hdrTarget;
    if (aaMode >= AAMode::SMAA_T2x && taaHistory[0])
        aaSource = taaHistory[1 - taaCurrentIdx];
    else if (aaMode == AAMode::SMAA_1x && smaaOutputTex)
        aaSource = smaaOutputTex;

    if (casEnabled && casPipeline && casOutputTex) {
        struct
        {
            glm::vec2 screenSize;
            float sharpness;
            float _p;
        } casUBO;
        casUBO.screenSize = glm::vec2((float)w, (float)h);
        casUBO.sharpness = casStrength;

        SDL_GPUStorageTextureReadWriteBinding cw = {.texture = casOutputTex};
        SDL_GPUComputePass* cpass = SDL_BeginGPUComputePass(cmd, &cw, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(cpass, casPipeline);
        SDL_GPUTextureSamplerBinding csamp = {.texture = aaSource, .sampler = tonemapSampler};
        SDL_BindGPUComputeSamplers(cpass, 0, &csamp, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &casUBO, sizeof(casUBO));
        SDL_DispatchGPUCompute(cpass, (w + 15) / 16, (h + 15) / 16, 1);
        SDL_EndGPUComputePass(cpass);

        aaSource = casOutputTex;
    }

    // PASS 2: Tone mapping -> swapchain (or captureRT for screenshots)
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

            // Only use a post-processing texture if BOTH its toggle is on AND its
            // pipeline initialised successfully.  Otherwise fall back to a neutral
            // 1×1 texture so we never sample an uninitialised / never-written-to
            // texture (which triggers a GPU fault on the Metal backend).
            const bool useBloom = toggles.bloom && bloomDownsamplePipeline && bloomMips[0];
            const bool useSSAO = toggles.ssao && ssaoPipeline && ssaoBlurTexture;
            const bool useSSR = toggles.ssr && ssrPipeline && ssrTexture[ssrCurrentIdx];
            const bool useVol = toggles.volumetrics && volumetricPipeline && volumetricTexture;

            TonemapParamsUBO params{};
            params.exposure = 1.0f;
            params.gamma = 2.2f;
            params.tonemapMode = 0; // ACES
            params.bloomStrength = useBloom ? bloomStr : 0.0f;
            // When SMAA is active, AO is already baked into the SMAA output
            // (via the neighborhood blend pass). Don't apply it again in tonemap.
            params.ssaoStrength = (useSSAO && aaMode == AAMode::Off) ? ssaoStr : 0.0f;
            params.ssrStrength = useSSR ? ssrStr : 0.0f;
            params.volumetricStrength = useVol ? volStr : 0.0f;
            // Legacy unsharp mask disabled — SMAA handles edge quality.
            params.sharpenStrength = 0.0f;
            params.ssaoPower = ssaoPower;
            SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));

            // Bind all 5 post-process textures for compositing.
            const SDL_GPUTextureSamplerBinding tonemapSamplers[5] = {
                {.texture = aaSource, .sampler = tonemapSampler},
                {.texture = useBloom ? bloomMips[0] : fallbackBlack, .sampler = tonemapSampler},
                {.texture = useSSAO ? ssaoBlurTexture : fallbackWhite, .sampler = tonemapSampler},
                {.texture = useSSR ? ssrTexture[ssrCurrentIdx] : fallbackBlack, .sampler = tonemapSampler},
                {.texture = useVol ? volumetricTexture : fallbackBlack, .sampler = tonemapSampler},
            };
            SDL_BindGPUFragmentSamplers(pass, 0, tonemapSamplers, 5);

            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0); // fullscreen triangle
        }

        // ImGui overlay (in LDR, on top of tone-mapped image)
        if (drawData)
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, pass);

        // HUD overlay (in LDR, on top of everything)
        if (hudTexture_ && hudBlitPipeline_) {
            SDL_BindGPUGraphicsPipeline(pass, hudBlitPipeline_);
            SDL_GPUTextureSamplerBinding hudBinding{};
            hudBinding.texture = hudTexture_;
            hudBinding.sampler = hudSampler_;
            SDL_BindGPUFragmentSamplers(pass, 0, &hudBinding, 1);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0); // fullscreen triangle
        }

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

    const Uint64 submitT0 = SDL_GetPerformanceCounter();
    SDL_SubmitGPUCommandBuffer(cmd);
    const Uint64 submitT1 = SDL_GetPerformanceCounter();
    lastRecordMs = deltaMs(acquireT1, submitT0);
    lastSubmitMs = deltaMs(submitT0, submitT1);
    (void)drawT0; // total drawFrame ms is the caller's responsibility to time.

    if (!pendingCapPath.empty())
        downloadAndSaveCapture(w, h);
}

// Screenshot download

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

// Misc

int Renderer::loadSceneModel(
    const char* filename, glm::vec3 pos, float scale, bool flipUVs, const std::string& excludeNodesContaining)
{
    const char* const k_base = SDL_GetBasePath();
    char path[512];
    SDL_snprintf(path, sizeof(path), "%sassets/%s", k_base ? k_base : "", filename);

    LoadedModel loaded;
    if (!loadModel(path, loaded, flipUVs, excludeNodesContaining)) {
        SDL_Log("Renderer::loadSceneModel: failed to load '%s'", filename);
        return -1;
    }

    // ── DEBUG: dump vertex data stats for every loaded model ──
    SDL_Log("[LOAD-DBG] '%s': %zu mesh(es)", filename, loaded.meshes.size());
    for (size_t mi = 0; mi < loaded.meshes.size(); ++mi) {
        const auto& md = loaded.meshes[mi];
        SDL_Log("[LOAD-DBG]   mesh[%zu]: %zu verts, %zu indices", mi, md.vertices.size(), md.indices.size());
        // Compute bounds & print first 3 vertices
        glm::vec3 bmin(1e9f), bmax(-1e9f);
        for (size_t vi = 0; vi < md.vertices.size(); ++vi) {
            const auto& v = md.vertices[vi];
            bmin = glm::min(bmin, v.position);
            bmax = glm::max(bmax, v.position);
            if (vi < 3) {
                SDL_Log("[LOAD-DBG]     v[%zu] pos=(%.4f, %.4f, %.4f) nrm=(%.3f,%.3f,%.3f) uv=(%.3f,%.3f)",
                        vi,
                        v.position.x,
                        v.position.y,
                        v.position.z,
                        v.normal.x,
                        v.normal.y,
                        v.normal.z,
                        v.texCoord.x,
                        v.texCoord.y);
            }
        }
        SDL_Log("[LOAD-DBG]     bounds: min=(%.4f,%.4f,%.4f) max=(%.4f,%.4f,%.4f)",
                bmin.x,
                bmin.y,
                bmin.z,
                bmax.x,
                bmax.y,
                bmax.z);
        // Print first 6 indices (2 triangles)
        if (md.indices.size() >= 6) {
            SDL_Log("[LOAD-DBG]     first indices: %u %u %u | %u %u %u",
                    md.indices[0],
                    md.indices[1],
                    md.indices[2],
                    md.indices[3],
                    md.indices[4],
                    md.indices[5]);
        }
    }
    // ── END DEBUG ──

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

    // Queue the data — the actual GPU copy happens in drawFrame()'s command
    // buffer, batched with all other copies.  No separate submit, no stall.
    PendingVertexUpload upload;
    upload.dstBuffer = model.meshes[static_cast<size_t>(meshIndex)].vertexBuffer;
    upload.data.resize(bytes);
    SDL_memcpy(upload.data.data(), vertices, bytes);
    pendingVertexUploads.push_back(std::move(upload));
}

// ─── Skinned character pipeline (perf Phase 1B) ──────────────────────────────

bool Renderer::initSkinnedPipelines()
{
    // pbr_skinned.vert: 0 samplers, 1 UBO (Matrices), 2 storage buffers (palette, instances).
    SDL_GPUShader* vert =
        loadShaderFromFile("pbr_skinned.vert", SDL_GPU_SHADERSTAGE_VERTEX, /*samplers=*/0, /*ubos=*/1, /*ssbos=*/2);
    SDL_GPUShader* frag = loadShaderFromFile("pbr.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, /*samplers=*/8, /*ubos=*/3);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    // Two vertex buffers:
    //  slot 0 — ModelVertex (positions/normals/tex/tangent), shared with the rig template
    //  slot 1 — SkinVertex  (4 bone indices + 4 weights)
    const SDL_GPUVertexBufferDescription vbDescs[2] = {
        {.slot = 0,
         .pitch = sizeof(ModelVertex),
         .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
         .instance_step_rate = 0},
        {.slot = 1, .pitch = sizeof(SkinVertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX, .instance_step_rate = 0},
    };

    const SDL_GPUVertexAttribute attrs[6] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12},
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24},
        {.location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 32},
        {.location = 4, .buffer_slot = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_INT4, .offset = 0},
        {.location = 5, .buffer_slot = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 16},
    };

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = vbDescs;
    vertexInput.num_vertex_buffers = 2;
    vertexInput.vertex_attributes = attrs;
    vertexInput.num_vertex_attributes = 6;

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
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // matches pbrPipeline (rig may be double-sided)
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;

    pbrSkinnedPipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);
    if (!pbrSkinnedPipeline) {
        SDL_Log("Renderer: pbrSkinnedPipeline creation failed: %s", SDL_GetError());
        return false;
    }

    // Shadow variant — depth-only, position + bone data.
    SDL_GPUShader* shadowVert =
        loadShaderFromFile("shadow_skinned.vert", SDL_GPU_SHADERSTAGE_VERTEX, /*samplers=*/0, /*ubos=*/1, /*ssbos=*/2);
    SDL_GPUShader* shadowFrag = loadShaderFromFile("shadow.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!shadowVert || !shadowFrag) {
        SDL_ReleaseGPUShader(device, shadowVert);
        SDL_ReleaseGPUShader(device, shadowFrag);
        return false;
    }

    // Same two-buffer vertex input but only positions + bone data are referenced.
    SDL_GPUGraphicsPipelineCreateInfo spci{};
    spci.vertex_shader = shadowVert;
    spci.fragment_shader = shadowFrag;
    spci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    spci.vertex_input_state = vertexInput;
    spci.target_info.num_color_targets = 0;
    spci.target_info.has_depth_stencil_target = true;
    spci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    spci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    spci.depth_stencil_state.enable_depth_test = true;
    spci.depth_stencil_state.enable_depth_write = true;
    spci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    spci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT; // mirror legacy shadow pipeline (Phase 3)
    spci.rasterizer_state.depth_bias_constant_factor = 0.75f;
    spci.rasterizer_state.depth_bias_slope_factor = 1.0f;
    spci.rasterizer_state.enable_depth_bias = true;

    shadowSkinnedPipeline = SDL_CreateGPUGraphicsPipeline(device, &spci);
    SDL_ReleaseGPUShader(device, shadowVert);
    SDL_ReleaseGPUShader(device, shadowFrag);
    if (!shadowSkinnedPipeline) {
        SDL_Log("Renderer: shadowSkinnedPipeline creation failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

bool Renderer::setSkinnedRig(const LoadedModel& model,
                             const std::vector<std::vector<SkinVertex>>& skinPerMesh,
                             int numJoints)
{
    if (skinnedRigInstalled) {
        SDL_Log("Renderer::setSkinnedRig: rig already installed; ignoring");
        return false;
    }
    if (model.meshes.size() != skinPerMesh.size()) {
        SDL_Log("Renderer::setSkinnedRig: mesh / skin-data count mismatch (%zu vs %zu)",
                model.meshes.size(),
                skinPerMesh.size());
        return false;
    }

    // Reuse the existing model upload path to get vertex/index/textures onto the
    // GPU.  We then attach a parallel SkinVertex buffer per mesh.
    ModelInstance tmp;
    if (!uploadModel(model, tmp))
        return false;

    skinnedNumJoints = numJoints;
    skinnedTextures = tmp.textures;
    skinnedMeshes.clear();
    skinnedMeshes.reserve(tmp.meshes.size());

    // Single big transfer buffer for all bone-data uploads (one mesh at a time).
    Uint32 maxBoneBytes = 0;
    for (const auto& sv : skinPerMesh)
        maxBoneBytes = std::max(maxBoneBytes, static_cast<Uint32>(sv.size() * sizeof(SkinVertex)));
    SDL_GPUTransferBuffer* boneXfer = nullptr;
    if (maxBoneBytes > 0) {
        SDL_GPUTransferBufferCreateInfo tbInfo{};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = maxBoneBytes;
        boneXfer = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    for (size_t m = 0; m < tmp.meshes.size(); ++m) {
        const auto& srcMesh = tmp.meshes[m];
        const auto& boneData = skinPerMesh[m];

        SkinnedMesh sm;
        sm.vertexBuffer = srcMesh.vertexBuffer;
        sm.indexBuffer = srcMesh.indexBuffer;
        sm.indexCount = srcMesh.indexCount;
        sm.albedoTexIndex = srcMesh.albedoTexIndex;
        sm.normalTexIndex = srcMesh.normalTexIndex;
        sm.metallicRoughnessTexIndex = srcMesh.metallicRoughnessTexIndex;
        sm.emissiveTexIndex = srcMesh.emissiveTexIndex;
        sm.material = srcMesh.material;

        const Uint32 bytes = static_cast<Uint32>(boneData.size() * sizeof(SkinVertex));
        if (bytes > 0) {
            SDL_GPUBufferCreateInfo bInfo{};
            bInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bInfo.size = bytes;
            sm.boneBuffer = SDL_CreateGPUBuffer(device, &bInfo);

            if (sm.boneBuffer && boneXfer) {
                void* mapped = SDL_MapGPUTransferBuffer(device, boneXfer, /*cycle=*/true);
                if (mapped) {
                    SDL_memcpy(mapped, boneData.data(), bytes);
                    SDL_UnmapGPUTransferBuffer(device, boneXfer);
                    SDL_GPUTransferBufferLocation src{};
                    src.transfer_buffer = boneXfer;
                    src.offset = 0;
                    SDL_GPUBufferRegion dst{};
                    dst.buffer = sm.boneBuffer;
                    dst.offset = 0;
                    dst.size = bytes;
                    SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/false);
                }
            }
        }

        skinnedMeshes.push_back(sm);
    }

    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);

    if (boneXfer)
        SDL_ReleaseGPUTransferBuffer(device, boneXfer);

    // We deliberately keep srcMesh's VB/IB; they were created by uploadModel and
    // will be released in quit() via skinnedMeshes.  The temporary ModelInstance
    // owned them; transfer ownership now by leaving `tmp` to drop.
    tmp.meshes.clear();
    tmp.textures.clear();

    skinnedRigInstalled = true;
    SDL_Log("Renderer: skinned rig installed — %zu mesh(es), %d joints", skinnedMeshes.size(), numJoints);
    return true;
}

void Renderer::setSkinnedFrame(const std::vector<glm::mat4>& palette, const std::vector<SkinnedInstance>& instances)
{
    skinnedFramePalette = palette;
    skinnedFrameInstances = instances;
    skinnedFrameDirty = !instances.empty();
}

bool Renderer::ensureSkinnedSSBOs(Uint32 paletteBytes, Uint32 instanceBytes)
{
    auto growBuf = [&](SDL_GPUBuffer*& buf, Uint32& cap, Uint32 want, SDL_GPUBufferUsageFlags use) {
        if (want == 0)
            return true;
        if (want > cap) {
            if (buf)
                SDL_ReleaseGPUBuffer(device, buf);
            SDL_GPUBufferCreateInfo bInfo{};
            bInfo.usage = use;
            bInfo.size = want;
            buf = SDL_CreateGPUBuffer(device, &bInfo);
            cap = buf ? want : 0;
            return buf != nullptr;
        }
        return true;
    };
    auto growXfer = [&](SDL_GPUTransferBuffer*& xfer, Uint32& cap, Uint32 want) {
        if (want == 0)
            return true;
        if (want > cap) {
            if (xfer)
                SDL_ReleaseGPUTransferBuffer(device, xfer);
            SDL_GPUTransferBufferCreateInfo tbInfo{};
            tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbInfo.size = want;
            xfer = SDL_CreateGPUTransferBuffer(device, &tbInfo);
            cap = xfer ? want : 0;
            return xfer != nullptr;
        }
        return true;
    };
    if (!growBuf(skinnedPaletteSSBO, skinnedPaletteCapacity, paletteBytes, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ))
        return false;
    if (!growBuf(
            skinnedInstanceSSBO, skinnedInstanceCapacity, instanceBytes, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ))
        return false;
    if (!growXfer(skinnedPaletteXfer, skinnedPaletteXferCapacity, paletteBytes))
        return false;
    if (!growXfer(skinnedInstanceXfer, skinnedInstanceXferCapacity, instanceBytes))
        return false;
    return true;
}

void Renderer::uploadSkinnedFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUCopyPass* copyPass)
{
    if (!skinnedFrameDirty || skinnedFrameInstances.empty())
        return;

    const Uint32 paletteBytes = static_cast<Uint32>(skinnedFramePalette.size() * sizeof(glm::mat4));
    const Uint32 instanceBytes = static_cast<Uint32>(skinnedFrameInstances.size() * sizeof(SkinnedInstance));
    if (!ensureSkinnedSSBOs(paletteBytes, instanceBytes))
        return;

    auto upload = [&](SDL_GPUTransferBuffer* xfer, SDL_GPUBuffer* dst, const void* src, Uint32 bytes) {
        if (bytes == 0 || !xfer || !dst)
            return;
        void* mapped = SDL_MapGPUTransferBuffer(device, xfer, /*cycle=*/true);
        if (!mapped)
            return;
        SDL_memcpy(mapped, src, bytes);
        SDL_UnmapGPUTransferBuffer(device, xfer);
        SDL_GPUTransferBufferLocation s{};
        s.transfer_buffer = xfer;
        s.offset = 0;
        SDL_GPUBufferRegion d{};
        d.buffer = dst;
        d.offset = 0;
        d.size = bytes;
        SDL_UploadToGPUBuffer(copyPass, &s, &d, /*cycle=*/false);
    };
    upload(skinnedPaletteXfer, skinnedPaletteSSBO, skinnedFramePalette.data(), paletteBytes);
    upload(skinnedInstanceXfer, skinnedInstanceSSBO, skinnedFrameInstances.data(), instanceBytes);
    (void)cmd;
}

// ─── End skinned character pipeline ───────────────────────────────────────────

void Renderer::requestScreenshot(const std::string& path)
{
    pendingCapPath = path;
}

bool Renderer::setVSync(const bool enabled)
{
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!enabled) {
        // Bench profiling showed mailbox occasionally stalls on submit/acquire
        // waiting for queued swap images to roll over.  Honour an env override
        // (BENCH_PRESENT=mailbox|immediate|vsync) so we can A/B without
        // recompiling, but default to MAILBOX for normal play (smooth
        // tear-free frames) and let the bench harness flip to IMMEDIATE when
        // it wants the lowest-back-pressure path.
        const char* p = SDL_getenv("BENCH_PRESENT");
        if (p && SDL_strcasecmp(p, "immediate") == 0) {
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        } else if (p && SDL_strcasecmp(p, "mailbox") == 0) {
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
        } else if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX)) {
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
        } else {
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        }
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

    // Release skinned-mesh staging buffer.
    if (skinTransferBuf)
        SDL_ReleaseGPUTransferBuffer(device, skinTransferBuf);
    skinTransferBuf = nullptr;
    skinTransferBufSize = 0;

    // Release render targets.
    if (captureRT)
        SDL_ReleaseGPUTexture(device, captureRT);
    if (hdrTarget)
        SDL_ReleaseGPUTexture(device, hdrTarget);
    if (depthTexture)
        SDL_ReleaseGPUTexture(device, depthTexture);
    if (weaponDepthTexture)
        SDL_ReleaseGPUTexture(device, weaponDepthTexture);
    if (shadowMap)
        SDL_ReleaseGPUTexture(device, shadowMap);

    // Release IBL resources.
    if (brdfLUT)
        SDL_ReleaseGPUTexture(device, brdfLUT);
    if (envCubemap)
        SDL_ReleaseGPUTexture(device, envCubemap);
    if (irradianceMap)
        SDL_ReleaseGPUTexture(device, irradianceMap);
    if (prefilterMap)
        SDL_ReleaseGPUTexture(device, prefilterMap);
    if (irradianceWorkMap)
        SDL_ReleaseGPUTexture(device, irradianceWorkMap);
    if (prefilterWorkMap)
        SDL_ReleaseGPUTexture(device, prefilterWorkMap);
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

    // HUD blit
    if (hudBlitPipeline_)
        SDL_ReleaseGPUGraphicsPipeline(device, hudBlitPipeline_);
    if (hudSampler_)
        SDL_ReleaseGPUSampler(device, hudSampler_);

    // Release SMAA resources.
    if (smaaEdgeTex)
        SDL_ReleaseGPUTexture(device, smaaEdgeTex);
    if (smaaBlendTex)
        SDL_ReleaseGPUTexture(device, smaaBlendTex);
    if (smaaOutputTex)
        SDL_ReleaseGPUTexture(device, smaaOutputTex);
    if (smaaAreaTex)
        SDL_ReleaseGPUTexture(device, smaaAreaTex);
    if (smaaSearchTex)
        SDL_ReleaseGPUTexture(device, smaaSearchTex);
    if (casOutputTex)
        SDL_ReleaseGPUTexture(device, casOutputTex);

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
    if (smaaResolvePipeline)
        SDL_ReleaseGPUComputePipeline(device, smaaResolvePipeline);
    if (casPipeline)
        SDL_ReleaseGPUComputePipeline(device, casPipeline);
    if (brdfLutPipeline)
        SDL_ReleaseGPUComputePipeline(device, brdfLutPipeline);
    if (irradiancePipeline)
        SDL_ReleaseGPUComputePipeline(device, irradiancePipeline);
    if (prefilterPipeline)
        SDL_ReleaseGPUComputePipeline(device, prefilterPipeline);

    // Release SMAA graphics pipelines.
    if (smaaEdgePipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, smaaEdgePipeline);
    if (smaaBlendPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, smaaBlendPipeline);
    if (smaaNeighborhoodPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, smaaNeighborhoodPipeline);

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
    if (nearestDepthSampler)
        SDL_ReleaseGPUSampler(device, nearestDepthSampler);

    // Release pipelines.
    ImGui_ImplSDLGPU3_Shutdown();
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
