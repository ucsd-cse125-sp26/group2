// stb_truetype implementation — compiled exactly once here
#define STB_TRUETYPE_IMPLEMENTATION
#include "SdfAtlas.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <stb_truetype.h>
#include <vector>

// ---------------------------------------------------------------------------
// Shelf-packer for glyph atlas
// ---------------------------------------------------------------------------

struct Shelf
{
    int x = 0, y = 0, h = 0;
};

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

// ---------------------------------------------------------------------------
// Brute-force SDF bake for one glyph bitmap
// ---------------------------------------------------------------------------

static void bakeSdf(const uint8_t* bmp, int bw, int bh, uint8_t* out, int spread)
{
    for (int py = 0; py < bh; ++py) {
        for (int px = 0; px < bw; ++px) {
            const bool inside = bmp[py * bw + px] > 127;
            float minDist = static_cast<float>(spread);

            const int sx0 = std::max(0, px - spread);
            const int sx1 = std::min(bw - 1, px + spread);
            const int sy0 = std::max(0, py - spread);
            const int sy1 = std::min(bh - 1, py + spread);

            for (int sy = sy0; sy <= sy1; ++sy) {
                for (int sx = sx0; sx <= sx1; ++sx) {
                    const bool other = bmp[sy * bw + sx] > 127;
                    if (other != inside) {
                        const float dx = static_cast<float>(sx - px);
                        const float dy = static_cast<float>(sy - py);
                        const float d = std::sqrt(dx * dx + dy * dy);
                        minDist = std::min(minDist, d);
                    }
                }
            }

            const float signedDist = inside ? minDist : -minDist;
            const float norm = 0.5f + signedDist / (static_cast<float>(spread) * 2.f);
            out[py * bw + px] = static_cast<uint8_t>(std::clamp(norm, 0.f, 1.f) * 255.f);
        }
    }
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

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

    // Bake ASCII 32–126
    for (int cp = 32; cp <= 126; ++cp) {
        int ix0, iy0, ix1, iy1;
        stbtt_GetCodepointBitmapBox(&font, cp, scale_, scale_, &ix0, &iy0, &ix1, &iy1);
        const int gw = ix1 - ix0;
        const int gh = iy1 - iy0;
        if (gw <= 0 || gh <= 0)
            continue;

        // Rasterise
        const int padded = k_spread * 2;
        const int pw = gw + padded;
        const int ph = gh + padded;
        std::vector<uint8_t> bmp(pw * ph, 0);
        stbtt_MakeCodepointBitmapSubpixel(&font,
                                          bmp.data() + k_spread * pw + k_spread, // offset into padded buffer
                                          gw,
                                          gh,
                                          pw,
                                          scale_,
                                          scale_,
                                          0.f,
                                          0.f,
                                          cp);

        // SDF bake
        std::vector<uint8_t> sdf(pw * ph);
        bakeSdf(bmp.data(), pw, ph, sdf.data(), k_spread);

        // Pack into atlas
        int ax, ay;
        if (!packRect(shelves, k_atlasW, k_atlasH, pw, ph, ax, ay)) {
            SDL_Log("SdfAtlas: atlas full at codepoint %d", cp);
            break;
        }

        // Copy SDF into atlas
        for (int row = 0; row < ph; ++row)
            std::memcpy(&atlas[(ay + row) * k_atlasW + ax], &sdf[row * pw], pw);

        // Glyph metrics
        int advW, lsb;
        stbtt_GetCodepointHMetrics(&font, cp, &advW, &lsb);
        GlyphInfo g{};
        g.uvMin = {static_cast<float>(ax) / k_atlasW, static_cast<float>(ay) / k_atlasH};
        g.uvMax = {static_cast<float>(ax + pw) / k_atlasW, static_cast<float>(ay + ph) / k_atlasH};
        g.bearing = {static_cast<float>(ix0 - k_spread), static_cast<float>(-iy0 + k_spread)};
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
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

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

    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
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

const GlyphInfo* SdfAtlas::glyph(uint32_t codepoint) const
{
    auto it = glyphs_.find(codepoint);
    return (it != glyphs_.end()) ? &it->second : nullptr;
}

void SdfAtlas::bindFragment(SDL_GPURenderPass* pass, uint32_t slot) const
{
    if (texture_ && sampler_) {
        SDL_GPUTextureSamplerBinding tsb{texture_, sampler_};
        SDL_BindGPUFragmentSamplers(pass, slot, &tsb, 1);
    }
}
