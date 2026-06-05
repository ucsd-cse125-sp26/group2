/// @file HudSvgAtlas.cpp
/// @brief Runtime GPU atlas for LunaSVG-rasterized HUD SVG assets.

#include "HudSvgAtlas.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstring>

bool HudSvgAtlas::init(SDL_GPUDevice* device, std::filesystem::path assetDir)
{
    device_ = device;
    assetDir_ = std::move(assetDir);
    return createGpuResources();
}

void HudSvgAtlas::quit()
{
    if (device_) {
        SDL_ReleaseGPUSampler(device_, sampler_);
        SDL_ReleaseGPUTexture(device_, texture_);
    }
    sampler_ = nullptr;
    texture_ = nullptr;
    device_ = nullptr;
    sprites_.clear();
    cursorX_ = 0;
    cursorY_ = 0;
    rowH_ = 0;
}

std::optional<HudSvgSprite> HudSvgAtlas::sprite(HudIcon id, int width, int height)
{
    if (!device_ || id == HudIcon::None)
        return std::nullopt;

    const int safeW = std::clamp(width, 1, kAtlasSize - kPadding * 2);
    const int safeH = std::clamp(height, 1, kAtlasSize - kPadding * 2);
    const CacheKey key{id, safeW, safeH};
    if (auto found = sprites_.find(key); found != sprites_.end())
        return found->second;

    std::string_view filename = hudSvgFilename(id);
    if (filename.empty())
        return std::nullopt;

    std::optional<HudSvgSprite> allocated = allocate(id, safeW, safeH);
    if (!allocated) {
        resetPacking();
        allocated = allocate(id, safeW, safeH);
    }
    if (!allocated)
        return std::nullopt;

    HudSvgBitmap bitmap = rasterizeHudSvg(assetDir_ / filename, safeW, safeH);
    if (bitmap.empty())
        return std::nullopt;

    if (!uploadSlot(bitmap, *allocated))
        return std::nullopt;

    sprites_.emplace(key, *allocated);
    return allocated;
}

bool HudSvgAtlas::createGpuResources()
{
    if (!device_)
        return false;

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.width = kAtlasSize;
    tci.height = kAtlasSize;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_ = SDL_CreateGPUTexture(device_, &tci);
    if (!texture_) {
        SDL_Log("HudSvgAtlas: failed to create %dx%d texture: %s", kAtlasSize, kAtlasSize, SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(device_, &sci);
    if (!sampler_) {
        SDL_Log("HudSvgAtlas: failed to create sampler: %s", SDL_GetError());
        return false;
    }

    return true;
}

void HudSvgAtlas::resetPacking()
{
    sprites_.clear();
    cursorX_ = 0;
    cursorY_ = 0;
    rowH_ = 0;
    SDL_Log("HudSvgAtlas: atlas cache reset after exhausting packed SVG slots");
}

std::optional<HudSvgSprite> HudSvgAtlas::allocate(HudIcon id, int width, int height)
{
    (void)id;
    const int slotW = width + kPadding * 2;
    const int slotH = height + kPadding * 2;
    if (slotW > kAtlasSize || slotH > kAtlasSize) {
        SDL_Log("HudSvgAtlas: SVG sprite %dx%d is larger than atlas", width, height);
        return std::nullopt;
    }

    if (cursorX_ + slotW > kAtlasSize) {
        cursorX_ = 0;
        cursorY_ += rowH_;
        rowH_ = 0;
    }
    if (cursorY_ + slotH > kAtlasSize) {
        return std::nullopt;
    }

    const int imageX = cursorX_ + kPadding;
    const int imageY = cursorY_ + kPadding;
    cursorX_ += slotW;
    rowH_ = std::max(rowH_, slotH);

    return HudSvgSprite{
        static_cast<float>(imageX) / static_cast<float>(kAtlasSize),
        static_cast<float>(imageY) / static_cast<float>(kAtlasSize),
        static_cast<float>(imageX + width) / static_cast<float>(kAtlasSize),
        static_cast<float>(imageY + height) / static_cast<float>(kAtlasSize),
        width,
        height,
    };
}

bool HudSvgAtlas::uploadSlot(const HudSvgBitmap& bitmap, const HudSvgSprite& sprite)
{
    const int imageX = static_cast<int>(sprite.u0 * static_cast<float>(kAtlasSize) + 0.5f);
    const int imageY = static_cast<int>(sprite.v0 * static_cast<float>(kAtlasSize) + 0.5f);
    const int slotX = imageX - kPadding;
    const int slotY = imageY - kPadding;
    const int slotW = bitmap.width + kPadding * 2;
    const int slotH = bitmap.height + kPadding * 2;

    std::vector<unsigned char> slot(static_cast<std::size_t>(slotW) * static_cast<std::size_t>(slotH) * 4u, 0);
    auto copyPixel = [&](int dstX, int dstY, int srcX, int srcY) {
        srcX = std::clamp(srcX, 0, bitmap.width - 1);
        srcY = std::clamp(srcY, 0, bitmap.height - 1);
        const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(bitmap.width) +
                                      static_cast<std::size_t>(srcX)) *
                                     4u;
        const std::size_t dstIndex =
            (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(slotW) + static_cast<std::size_t>(dstX)) * 4u;
        std::memcpy(slot.data() + dstIndex, bitmap.rgba.data() + srcIndex, 4);
    };

    for (int y = 0; y < slotH; ++y) {
        for (int x = 0; x < slotW; ++x)
            copyPixel(x, y, x - kPadding, y - kPadding);
    }

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = static_cast<Uint32>(slot.size());
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbci);
    if (!transfer) {
        SDL_Log("HudSvgAtlas: failed to create transfer buffer: %s", SDL_GetError());
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (!mapped) {
        SDL_Log("HudSvgAtlas: failed to map transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return false;
    }
    std::memcpy(mapped, slot.data(), slot.size());
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.pixels_per_row = static_cast<unsigned>(slotW);
    src.rows_per_layer = static_cast<unsigned>(slotH);

    SDL_GPUTextureRegion dst{};
    dst.texture = texture_;
    dst.x = static_cast<unsigned>(slotX);
    dst.y = static_cast<unsigned>(slotY);
    dst.w = static_cast<unsigned>(slotW);
    dst.h = static_cast<unsigned>(slotH);
    dst.d = 1;
    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return true;
}
