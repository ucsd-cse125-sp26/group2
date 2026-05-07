/// @file HudRenderer.hpp
/// @brief GPU backend for the HUD system — owns offscreen target, pipeline,
///        and vertex buffer upload.

#pragma once

#include "HudTypes.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <span>

class SdfAtlas;

/// @brief Renders batched HUD geometry to an offscreen RGBA8 texture.
///
/// Owns the GPU pipeline, offscreen render target, dynamic vertex buffer,
/// and sampler bindings.  The renderer (legacy/new) reads getOutputTexture()
/// and blits it after tone mapping.
class HudRenderer
{
public:
    /// @brief Initialise GPU resources.
    /// @param device       Shared SDL GPU device.
    /// @param shaderFormat SPIR-V, MSL, or DXIL.
    /// @param sdfAtlas     Font atlas for SDF text rendering.
    /// @param screenW      Initial viewport width.
    /// @param screenH      Initial viewport height.
    /// @return true on success.
    bool init(SDL_GPUDevice* device,
              SDL_GPUShaderFormat shaderFormat,
              const SdfAtlas& sdfAtlas,
              uint32_t screenW,
              uint32_t screenH);

    /// @brief Release all GPU resources.
    void quit();

    /// @brief Recreate the offscreen target on window resize.
    void resize(uint32_t newW, uint32_t newH);

    /// @brief Upload vertex data, execute the HUD render pass, and submit.
    ///
    /// Acquires its own command buffer, clears the offscreen target to
    /// transparent black, draws all batched quads, and submits.
    /// @param vertices Flat vertex array produced by HudContext.
    /// @param clipRects Parallel array of clip rects for scissor state changes.
    ///                  Each entry is {startVertex, vertexCount, x, y, w, h}.
    void render(std::span<const HudVertex> vertices, std::span<const std::array<float, 6>> clipRects);

    /// @brief The offscreen texture to be blitted by the renderer.
    [[nodiscard]] SDL_GPUTexture* getOutputTexture() const { return offscreenTarget_; }

    /// @brief Current target width.
    [[nodiscard]] uint32_t width() const { return width_; }

    /// @brief Current target height.
    [[nodiscard]] uint32_t height() const { return height_; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;

    // Offscreen targets — render into the multisample target, auto-resolve
    // into the 1× sampleable target at end-of-pass. The 4× MSAA gives the
    // entire HUD pass free geometry-edge AA on every diagonal icon stroke,
    // chevron, ring, and rotated rect, which were previously hard-aliased.
    SDL_GPUTexture* msaaTarget_ = nullptr;      ///< 4× multisampled, COLOR_TARGET only.
    SDL_GPUTexture* offscreenTarget_ = nullptr; ///< 1×, COLOR_TARGET | SAMPLER (resolve dst).
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    static constexpr SDL_GPUSampleCount k_sampleCount = SDL_GPU_SAMPLECOUNT_4;

    // Pipeline
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;

    // Samplers (bound to fragment set 1)
    SDL_GPUTexture* sdfAtlasTex_ = nullptr;   ///< Non-owning: from SdfAtlas.
    SDL_GPUSampler* sdfAtlasSamp_ = nullptr;  ///< Non-owning: from SdfAtlas.
    SDL_GPUTexture* iconAtlasTex_ = nullptr;  ///< Owning: 1x1 white fallback until real atlas.
    SDL_GPUSampler* iconAtlasSamp_ = nullptr; ///< Owning.

    // Dynamic vertex buffer (recreated if capacity exceeded)
    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUTransferBuffer* transferBuffer_ = nullptr;
    uint32_t vertexCapacity_ = 0; ///< Current buffer capacity in vertices.

    // Uniform buffer data
    struct ScreenUniforms
    {
        float screenW;
        float screenH;
    };

    bool createOffscreenTarget(uint32_t w, uint32_t h);
    bool createPipeline();
    bool ensureVertexBuffer(uint32_t requiredVertices);

    /// @brief Load a compiled shader from the shaders/ directory.
    SDL_GPUShader*
    loadShader(const char* name, SDL_GPUShaderStage stage, uint32_t samplerCount, uint32_t uniformBufferCount);
};
