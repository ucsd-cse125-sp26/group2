/// @file HudSvgAtlas.hpp
/// @brief Runtime GPU atlas for LunaSVG-rasterized HUD SVG assets.

#pragma once

#include "HudSvgRaster.hpp"

#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

struct HudSvgSprite
{
    float u0 = 0.f;
    float v0 = 0.f;
    float u1 = 0.f;
    float v1 = 0.f;
    int width = 0;
    int height = 0;
};

class HudSvgAtlas
{
public:
    bool init(SDL_GPUDevice* device, std::filesystem::path assetDir);
    void quit();

    [[nodiscard]] std::optional<HudSvgSprite> sprite(HudIcon id, int width, int height);
    [[nodiscard]] SDL_GPUTexture* texture() const { return texture_; }
    [[nodiscard]] SDL_GPUSampler* sampler() const { return sampler_; }

private:
    struct CacheKey
    {
        HudIcon id = HudIcon::None;
        int width = 0;
        int height = 0;

        bool operator==(const CacheKey& other) const
        {
            return id == other.id && width == other.width && height == other.height;
        }
    };

    struct CacheKeyHash
    {
        std::size_t operator()(const CacheKey& key) const
        {
            const auto id = static_cast<std::size_t>(key.id);
            return id ^ (static_cast<std::size_t>(key.width) << 8u) ^ (static_cast<std::size_t>(key.height) << 24u);
        }
    };

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTexture* texture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    std::filesystem::path assetDir_;

    static constexpr int kAtlasSize = 4096;
    static constexpr int kPadding = 2;

    int cursorX_ = 0;
    int cursorY_ = 0;
    int rowH_ = 0;

    std::unordered_map<CacheKey, HudSvgSprite, CacheKeyHash> sprites_;

    [[nodiscard]] bool createGpuResources();
    [[nodiscard]] std::optional<HudSvgSprite> allocate(HudIcon id, int width, int height);
    [[nodiscard]] bool uploadSlot(const HudSvgBitmap& bitmap, const HudSvgSprite& sprite);
};

