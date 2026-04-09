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
    bool init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt);
    void quit();

    // ── Upload staging arrays to GPU (must be before render pass) ──────────

    void uploadBillboards(SDL_GPUCommandBuffer* cmd, const BillboardParticle* data, uint32_t count);
    void uploadTracers(SDL_GPUCommandBuffer* cmd, const TracerParticle* data, uint32_t count);
    void uploadRibbon(SDL_GPUCommandBuffer* cmd, const RibbonVertex* data, uint32_t count);
    void uploadHitscan(SDL_GPUCommandBuffer* cmd, const HitscanBeam* data, uint32_t count);
    void uploadArcs(SDL_GPUCommandBuffer* cmd, const ArcVertex* data, uint32_t count);
    void uploadSmoke(SDL_GPUCommandBuffer* cmd, const SmokeParticle* data, uint32_t count);
    void uploadDecals(SDL_GPUCommandBuffer* cmd, const DecalInstance* data, uint32_t count);
    void uploadSdfWorld(SDL_GPUCommandBuffer* cmd, const SdfGlyphGPU* data, uint32_t count);
    void uploadSdfHud(SDL_GPUCommandBuffer* cmd, const SdfGlyphGPU* data, uint32_t count);

    // ── Draw calls (inside render pass) ────────────────────────────────────

    /// @brief Issue all particle draw calls in the correct blend/depth order.
    /// @param pass   The active render pass.
    /// @param cmd    The command buffer (for push uniforms).
    /// @param screenW/H  Window dimensions for HUD uniform.
    void drawAll(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, float screenW, float screenH);

    // ── Smoke noise texture (created at init) ──────────────────────────────
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

    // ── Pipelines ──────────────────────────────────────────────────────────
    SDL_GPUGraphicsPipeline* billboardPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* tracerPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* ribbonPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* hitscanPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* arcPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* smokePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* decalPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* sdfWorldPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* sdfHudPipeline_ = nullptr;

    // ── Shared index buffer for instanced quad draws {0,1,2,2,3,0} × N ────
    SDL_GPUBuffer* quadIndexBuf_ = nullptr;
    static constexpr uint32_t k_maxQuadInstances = 4096;

    // ── GPU buffers ────────────────────────────────────────────────────────
    GpuParticleBuffer billboardBuf_; // storage
    GpuParticleBuffer tracerBuf_;    // storage
    GpuParticleBuffer ribbonBuf_;    // vertex
    GpuParticleBuffer hitscanBuf_;   // storage
    GpuParticleBuffer arcBuf_;       // vertex
    GpuParticleBuffer smokeBuf_;     // storage
    GpuParticleBuffer decalBuf_;     // storage
    GpuParticleBuffer sdfWorldBuf_;  // storage
    GpuParticleBuffer sdfHudBuf_;    // storage

    // ── Smoke noise texture ────────────────────────────────────────────────
    SDL_GPUTexture* smokeNoise_ = nullptr;
    SDL_GPUSampler* smokeSampler_ = nullptr;

    // ── Bullet hole / decal atlas (R8G8B8A8, procedural) ──────────────────
    SDL_GPUTexture* decalTex_ = nullptr;
    SDL_GPUSampler* decalSamp_ = nullptr;

    // ── SDF atlas (registered from outside after SdfAtlas::init) ──────────
    SDL_GPUTexture* sdfAtlasTex_ = nullptr;
    SDL_GPUSampler* sdfAtlasSamp_ = nullptr;

    // ── Internal helpers ───────────────────────────────────────────────────
    [[nodiscard]] bool buildPipelines();
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
    [[nodiscard]] SDL_GPUGraphicsPipeline*
    makeVertexPipeline(const char* vertName,
                       const char* fragName,
                       SDL_GPUVertexInputState vertexInput,
                       SDL_GPUColorTargetBlendState blend,
                       bool depthTest,
                       bool depthWrite,
                       SDL_GPUPrimitiveType prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);
    void buildQuadIndexBuffer();
    void buildSmokeNoise();
    void buildDecalTexture();

    static SDL_GPUColorTargetBlendState additiveBlend();
    static SDL_GPUColorTargetBlendState premulAlphaBlend();
    static SDL_GPUColorTargetBlendState alphaBlend();
};
