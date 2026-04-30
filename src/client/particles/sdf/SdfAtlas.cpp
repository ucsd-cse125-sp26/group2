/// @file SdfAtlas.cpp
/// @brief SDF glyph atlas baking and GPU upload implementation.

// stb_truetype implementation -- compiled exactly once here
#define STB_TRUETYPE_IMPLEMENTATION
#include "SdfAtlas.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <glm/glm.hpp>
#include <stb_truetype.h>
#include <vector>

// Shelf-packer for glyph atlas

/// @brief Single shelf row used by the shelf-packing algorithm.
struct Shelf
{
    int x = 0, y = 0, h = 0;
};

/// @brief Try to pack a rectangle into the atlas using shelf-packing.
/// @param shelves Current shelf state.
/// @param atlasW Atlas width in pixels.
/// @param atlasH Atlas height in pixels.
/// @param w Rectangle width to pack.
/// @param h Rectangle height to pack.
/// @param outX Output x position in the atlas.
/// @param outY Output y position in the atlas.
/// @return true if the rectangle was packed successfully.
static bool packRect(std::vector<Shelf>& shelves, int atlasW, int atlasH, int w, int h, int& outX, int& outY)
{
    const int padding = 2;
    const int fw = w + padding;
    const int fh = h + padding;

    for (auto& s : shelves) {
        if (s.x + fw <= atlasW && fh <= s.h) {
            outX = s.x;
            outY = s.y;
            s.x += fw;
            return true;
        }
    }
    // Start a new shelf
    int newY = shelves.empty() ? 0 : shelves.back().y + shelves.back().h;
    if (newY + fh > atlasH)
        return false;
    Shelf ns{fw, newY, fh};
    outX = 0;
    outY = newY;
    shelves.push_back(ns);
    return true;
}

// init

bool SdfAtlas::init(SDL_GPUDevice* dev, const char* ttfPath)
{
    device_ = dev;

    size_t fontDataSize = 0;
    void* fontData = SDL_LoadFile(ttfPath, &fontDataSize);
    if (!fontData) {
        SDL_Log("SdfAtlas: failed to load font %s: %s", ttfPath, SDL_GetError());
        return false;
    }

    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, static_cast<const uint8_t*>(fontData), 0)) {
        SDL_Log("SdfAtlas: stbtt_InitFont failed for %s", ttfPath);
        SDL_free(fontData);
        return false;
    }

    scale_ = stbtt_ScaleForPixelHeight(&font, static_cast<float>(k_renderPx));

    std::vector<uint8_t> atlas(k_atlasW * k_atlasH, 0);
    std::vector<Shelf> shelves;

    // SDF encoding: on-edge = 128, scale = 128 byte-units per k_spread pixels.
    // This maps to [0, 255] with 0.5 (≈128/255) at the glyph boundary,
    // matching the shader threshold of 0.5.
    const unsigned char onEdgeValue = 128;
    const float pixelDistScale = 128.0f / static_cast<float>(k_spread);

    // Bake ASCII 32-126 using stb_truetype's analytic SDF (vector-outline based).
    for (int cp = 32; cp <= 126; ++cp) {
        int pw, ph, xoff, yoff;
        unsigned char* sdf =
            stbtt_GetCodepointSDF(&font, scale_, cp, k_spread, onEdgeValue, pixelDistScale, &pw, &ph, &xoff, &yoff);
        if (!sdf || pw <= 0 || ph <= 0) {
            if (sdf)
                stbtt_FreeSDF(sdf, nullptr);
            // Whitespace glyphs (e.g. space) have no pixels but still need
            // an advance width so text layout inserts the correct gap.
            int advW, lsb;
            stbtt_GetCodepointHMetrics(&font, cp, &advW, &lsb);
            if (advW > 0) {
                GlyphInfo g{};
                g.advance = static_cast<float>(advW) * scale_;
                glyphs_[static_cast<uint32_t>(cp)] = g;
            }
            continue;
        }

        // Pack into atlas
        int ax, ay;
        if (!packRect(shelves, k_atlasW, k_atlasH, pw, ph, ax, ay)) {
            SDL_Log("SdfAtlas: atlas full at codepoint %d", cp);
            stbtt_FreeSDF(sdf, nullptr);
            break;
        }

        // Copy SDF into atlas
        for (int row = 0; row < ph; ++row)
            std::memcpy(&atlas[(ay + row) * k_atlasW + ax], &sdf[row * pw], pw);
        stbtt_FreeSDF(sdf, nullptr);

        // Glyph metrics.  xoff/yoff from stbtt_GetCodepointSDF already include
        // the spread padding, matching our previous bearing convention.
        int advW, lsb;
        stbtt_GetCodepointHMetrics(&font, cp, &advW, &lsb);
        GlyphInfo g{};
        g.uvMin = {static_cast<float>(ax) / k_atlasW, static_cast<float>(ay) / k_atlasH};
        g.uvMax = {static_cast<float>(ax + pw) / k_atlasW, static_cast<float>(ay + ph) / k_atlasH};
        g.bearing = {static_cast<float>(xoff), static_cast<float>(-yoff)};
        g.advance = static_cast<float>(advW) * scale_;
        g.width = static_cast<float>(pw);
        g.height = static_cast<float>(ph);
        glyphs_[static_cast<uint32_t>(cp)] = g;
    }

    SDL_free(fontData);

    // Upload atlas to GPU
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = k_atlasW;
    tci.height = k_atlasH;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ = SDL_CreateGPUTexture(dev, &tci);
    if (!texture_) {
        SDL_Log("SdfAtlas: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = k_atlasW * k_atlasH;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbci);
    void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
    std::memcpy(mapped, atlas.data(), k_atlasW * k_atlasH);
    SDL_UnmapGPUTransferBuffer(dev, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass* cpass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset = 0;
    src.pixels_per_row = k_atlasW;
    src.rows_per_layer = k_atlasH;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture_;
    dst.w = k_atlasW;
    dst.h = k_atlasH;
    dst.d = 1;

    SDL_UploadToGPUTexture(cpass, &src, &dst, false);
    SDL_EndGPUCopyPass(cpass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(dev, &sci);

    SDL_Log("SdfAtlas: baked %zu glyphs into %dx%d atlas", glyphs_.size(), k_atlasW, k_atlasH);
    return true;
}

/// @brief Release all GPU resources and reset internal state.
void SdfAtlas::quit()
{
    if (device_) {
        if (texture_)
            SDL_ReleaseGPUTexture(device_, texture_);
        if (sampler_)
            SDL_ReleaseGPUSampler(device_, sampler_);
    }
    texture_ = nullptr;
    sampler_ = nullptr;
    device_ = nullptr;
    glyphs_.clear();
}

/// @brief Look up glyph metrics by Unicode codepoint.
/// @param codepoint Unicode codepoint to look up.
/// @return Pointer to glyph info, or nullptr if the codepoint was not baked.
const GlyphInfo* SdfAtlas::glyph(uint32_t codepoint) const
{
    auto it = glyphs_.find(codepoint);
    return (it != glyphs_.end()) ? &it->second : nullptr;
}

/// @brief Bind the SDF atlas texture and sampler to a fragment sampler slot.
/// @param pass Active render pass.
/// @param slot Fragment sampler slot index.
void SdfAtlas::bindFragment(SDL_GPURenderPass* pass, uint32_t slot) const
{
    if (texture_ && sampler_) {
        SDL_GPUTextureSamplerBinding tsb{texture_, sampler_};
        SDL_BindGPUFragmentSamplers(pass, slot, &tsb, 1);
    }
}
