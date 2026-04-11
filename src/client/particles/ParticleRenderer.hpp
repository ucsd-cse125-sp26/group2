/// @file ParticleRenderer.hpp
/// @brief Owns all particle GPU pipelines and per-category GPU buffers.

#pragma once

#include "GpuParticleBuffer.hpp"
#include "ParticlePool.hpp"
#include "ParticleTypes.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

/// @brief Owns all particle GPU pipelines and per-category GPU buffers.
///
/// ParticleSystem calls uploadAll() (before render pass) and drawAll() (inside render pass).
class ParticleRenderer
{
public:
    /// @brief Initialise GPU pipelines, buffers, and procedural textures.
    /// @param dev       The SDL GPU device.
    /// @param colorFmt  Swapchain colour format.
    /// @param shaderFmt Shader binary format (SPIRV or MSL).
    /// @return True on success.
    bool init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt);

    /// @brief Release all GPU pipelines, buffers, and textures.
    void quit();

    // Upload staging arrays to GPU (must be before render pass)

    /// @brief Upload billboard particle data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to billboard particle array.
    /// @param count Number of particles to upload.
    void uploadBillboards(SDL_GPUCommandBuffer* cmd, const BillboardParticle* data, uint32_t count);

    /// @brief Upload tracer particle data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to tracer particle array.
    /// @param count Number of particles to upload.
    void uploadTracers(SDL_GPUCommandBuffer* cmd, const TracerParticle* data, uint32_t count);

    /// @brief Upload ribbon vertex data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to ribbon vertex array.
    /// @param count Number of vertices to upload.
    void uploadRibbon(SDL_GPUCommandBuffer* cmd, const RibbonVertex* data, uint32_t count);

    /// @brief Upload hitscan beam data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to hitscan beam array.
    /// @param count Number of beams to upload.
    void uploadHitscan(SDL_GPUCommandBuffer* cmd, const HitscanBeam* data, uint32_t count);

    /// @brief Upload lightning arc vertex data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to arc vertex array.
    /// @param count Number of vertices to upload.
    void uploadArcs(SDL_GPUCommandBuffer* cmd, const ArcVertex* data, uint32_t count);

    /// @brief Upload smoke particle data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to smoke particle array.
    /// @param count Number of particles to upload.
    void uploadSmoke(SDL_GPUCommandBuffer* cmd, const SmokeParticle* data, uint32_t count);

    /// @brief Upload decal instance data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to decal instance array.
    /// @param count Number of instances to upload.
    void uploadDecals(SDL_GPUCommandBuffer* cmd, const DecalInstance* data, uint32_t count);

    /// @brief Upload world-space SDF glyph data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to SDF glyph array.
    /// @param count Number of glyphs to upload.
    void uploadSdfWorld(SDL_GPUCommandBuffer* cmd, const SdfGlyphGPU* data, uint32_t count);

    /// @brief Upload HUD SDF glyph data to the GPU.
    /// @param cmd   Active command buffer.
    /// @param data  Pointer to SDF glyph array.
    /// @param count Number of glyphs to upload.
    void uploadSdfHud(SDL_GPUCommandBuffer* cmd, const SdfGlyphGPU* data, uint32_t count);

    // Draw calls (inside render pass)

    /// @brief Issue all particle draw calls in the correct blend/depth order.
    /// @param pass    The active render pass.
    /// @param cmd     The command buffer (for push uniforms).
    /// @param screenW Window width in pixels for HUD uniform.
    /// @param screenH Window height in pixels for HUD uniform.
    void drawAll(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, float screenW, float screenH);

    // Smoke noise texture (created at init)
    [[nodiscard]] SDL_GPUTexture* smokeNoiseTex() const { return smokeNoise_; }
    [[nodiscard]] SDL_GPUSampler* smokeSampler() const { return smokeSampler_; }

    /// @brief Register the SDF glyph atlas so drawAll() can bind it before drawing text.
    /// Call this once after SdfAtlas::init() succeeds.
    void setSdfAtlas(SDL_GPUTexture* tex, SDL_GPUSampler* samp)
    {
        sdfAtlasTex_ = tex;
        sdfAtlasSamp_ = samp;
    }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFmt_ = SDL_GPU_SHADERFORMAT_INVALID;
    SDL_GPUTextureFormat colorFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;

    // Pipelines
    SDL_GPUGraphicsPipeline* billboardPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* tracerPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* ribbonPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* hitscanPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* arcPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* smokePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* decalPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* sdfWorldPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* sdfHudPipeline_ = nullptr;

    // Shared index buffer for instanced quad draws {0,1,2,2,3,0} x N
    SDL_GPUBuffer* quadIndexBuf_ = nullptr;
    static constexpr uint32_t k_maxQuadInstances = 4096;

    // GPU buffers
    GpuParticleBuffer billboardBuf_; // storage
    GpuParticleBuffer tracerBuf_;    // storage
    GpuParticleBuffer ribbonBuf_;    // vertex
    GpuParticleBuffer hitscanBuf_;   // storage
    GpuParticleBuffer arcBuf_;       // vertex
    GpuParticleBuffer smokeBuf_;     // storage
    GpuParticleBuffer decalBuf_;     // storage
    GpuParticleBuffer sdfWorldBuf_;  // storage
    GpuParticleBuffer sdfHudBuf_;    // storage

    // Smoke noise texture
    SDL_GPUTexture* smokeNoise_ = nullptr;
    SDL_GPUSampler* smokeSampler_ = nullptr;

    // Bullet hole / decal atlas (R8G8B8A8, procedural)
    SDL_GPUTexture* decalTex_ = nullptr;
    SDL_GPUSampler* decalSamp_ = nullptr;

    // SDF atlas (registered from outside after SdfAtlas::init)
    SDL_GPUTexture* sdfAtlasTex_ = nullptr;
    SDL_GPUSampler* sdfAtlasSamp_ = nullptr;

    // Internal helpers
    /// @brief Create all graphics pipelines for each particle category.
    /// @return True if all pipelines were created successfully.
    [[nodiscard]] bool buildPipelines();
    /// @brief Create a pipeline that reads particle data from a storage buffer.
    /// @param vertName    Vertex shader filename (without path/extension).
    /// @param fragName    Fragment shader filename (without path/extension).
    /// @param storageBufs Number of storage buffers bound to the vertex stage.
    /// @param samplers    Number of texture samplers bound to the fragment stage.
    /// @param blend       Colour target blend state.
    /// @param depthTest   Enable depth testing.
    /// @param depthWrite  Enable depth writes.
    /// @param depthBias   Enable depth bias (for decals).
    /// @param prim        Primitive topology.
    /// @return The created pipeline, or nullptr on failure.
    [[nodiscard]] SDL_GPUGraphicsPipeline*
    makeStoragePipeline(const char* vertName,
                        const char* fragName,
                        uint32_t storageBufs,
                        uint32_t samplers,
                        SDL_GPUColorTargetBlendState blend,
                        bool depthTest,
                        bool depthWrite,
                        bool depthBias,
                        SDL_GPUPrimitiveType prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);

    /// @brief Create a pipeline that reads particle data from a vertex buffer.
    /// @param vertName    Vertex shader filename (without path/extension).
    /// @param fragName    Fragment shader filename (without path/extension).
    /// @param vertexInput Vertex input layout description.
    /// @param blend       Colour target blend state.
    /// @param depthTest   Enable depth testing.
    /// @param depthWrite  Enable depth writes.
    /// @param prim        Primitive topology.
    /// @return The created pipeline, or nullptr on failure.
    [[nodiscard]] SDL_GPUGraphicsPipeline*
    makeVertexPipeline(const char* vertName,
                       const char* fragName,
                       SDL_GPUVertexInputState vertexInput,
                       SDL_GPUColorTargetBlendState blend,
                       bool depthTest,
                       bool depthWrite,
                       SDL_GPUPrimitiveType prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);

    /// @brief Build the shared quad index buffer ({0,1,2,2,3,0} x N).
    void buildQuadIndexBuffer();

    /// @brief Generate and upload the procedural smoke noise texture.
    void buildSmokeNoise();

    /// @brief Generate and upload the procedural bullet-hole decal texture.
    void buildDecalTexture();

    /// @brief Return an additive blend state (src*alpha + dst*1).
    static SDL_GPUColorTargetBlendState additiveBlend();

    /// @brief Return a pre-multiplied alpha blend state (src*1 + dst*(1-srcAlpha)).
    static SDL_GPUColorTargetBlendState premulAlphaBlend();

    /// @brief Return a standard alpha blend state (src*alpha + dst*(1-srcAlpha)).
    static SDL_GPUColorTargetBlendState alphaBlend();
};
