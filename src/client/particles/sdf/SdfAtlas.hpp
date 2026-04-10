#pragma once

#include "SdfFont.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

/// @brief Loads a TTF font, bakes an SDF glyph atlas, and uploads it to the GPU.
///
/// Uses stb_truetype for glyph rasterization.  The SDF is computed on the CPU
/// (brute-force nearest-edge scan, spread = 8 px) and packed into a 1024×1024
/// R8_UNORM texture.
class SdfAtlas
{
public:
    static constexpr int k_atlasW = 1024;
    static constexpr int k_atlasH = 1024;
    static constexpr int k_renderPx = 48; ///< Rasterisation size in pixels.
    static constexpr int k_spread = 8;    ///< SDF spread in pixels.

    /// @brief Load TTF from path, bake SDF, upload to GPU.
    /// @return true on success.
    bool init(SDL_GPUDevice* dev, const char* ttfPath);

    void quit();

    /// @brief Look up glyph metrics. Returns nullptr if codepoint not baked.
    [[nodiscard]] const GlyphInfo* glyph(uint32_t codepoint) const;

    /// @brief Bind the SDF atlas texture + sampler to a fragment sampler slot.
    void bindFragment(SDL_GPURenderPass* pass, uint32_t slot) const;

    /// @brief Raw scale factor: bake pixels per em unit.
    [[nodiscard]] float scale() const { return scale_; }

    [[nodiscard]] bool ready() const { return texture_ != nullptr; }

    /// @brief GPU texture containing the baked SDF atlas (R8_UNORM, 1024×1024).
    [[nodiscard]] SDL_GPUTexture* gpuTexture() const { return texture_; }
    /// @brief Linear-clamp sampler for the SDF atlas.
    [[nodiscard]] SDL_GPUSampler* gpuSampler() const { return sampler_; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTexture* texture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    GlyphMap glyphs_;
    float scale_ = 1.f;
};
