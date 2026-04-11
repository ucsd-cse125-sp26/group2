/// @file SdfRenderer.cpp
/// @brief SDF text renderer implementation for world-space and screen-space text.

#include "SdfRenderer.hpp"

/// @brief Initialise the renderer by loading a font and baking the SDF atlas.
bool SdfRenderer::init(SDL_GPUDevice* dev, const char* ttfPath)
{
    return atlas_.init(dev, ttfPath);
}

/// @brief Release all resources owned by this renderer.
void SdfRenderer::quit()
{
    atlas_.quit();
    worldGlyphs_.clear();
    hudGlyphs_.clear();
}

/// @brief Clear all queued glyph data for the current frame.
void SdfRenderer::clear()
{
    worldGlyphs_.clear();
    hudGlyphs_.clear();
}

/// @brief Queue world-space billboard text (faces camera, depth-tested).
void SdfRenderer::drawWorldText(
    glm::vec3 worldPos, std::string_view text, glm::vec4 color, float worldHeight, glm::vec3 camRight, glm::vec3 camUp)
{
    if (!atlas_.ready())
        return;

    const float scale = worldHeight / static_cast<float>(SdfAtlas::k_renderPx);
    float cursorX = 0.f;

    for (unsigned char ch : text) {
        const GlyphInfo* g = atlas_.glyph(static_cast<uint32_t>(ch));
        if (!g) {
            cursorX += worldHeight * 0.5f;
            continue;
        }

        const float gw = g->width * scale;
        const float gh = g->height * scale;
        const float bx = g->bearing.x * scale;
        const float by = g->bearing.y * scale;

        // Bottom-left corner of this glyph
        const glm::vec3 bl = worldPos + camRight * (cursorX + bx) + camUp * (by - gh); // bearing.y is from baseline up

        SdfGlyphGPU gv{};
        gv.worldPos = bl;
        gv.size = gh;
        gv.uvMin = g->uvMin;
        gv.uvMax = g->uvMax;
        gv.color = color;
        gv.right = camRight;
        gv._p0 = 0.f;
        gv.up = camUp;
        gv._p1 = 0.f;
        // Width correction: glyph aspect embedded in uvMax-uvMin; shader scales by size*aspect
        (void)gw; // used implicitly via uvMax-uvMin in shader

        worldGlyphs_.push_back(gv);
        cursorX += g->advance * scale;
    }
}

/// @brief Queue HUD text in pixel coordinates (no depth test).
void SdfRenderer::drawScreenText(glm::vec2 pixelPos, std::string_view text, glm::vec4 color, float pixelHeight)
{
    if (!atlas_.ready())
        return;

    const float scale = pixelHeight / static_cast<float>(SdfAtlas::k_renderPx);
    float cursorX = 0.f;

    for (unsigned char ch : text) {
        const GlyphInfo* g = atlas_.glyph(static_cast<uint32_t>(ch));
        if (!g) {
            cursorX += pixelHeight * 0.5f;
            continue;
        }

        const float gw = g->width * scale;
        const float gh = g->height * scale;
        const float bx = g->bearing.x * scale;
        const float by = g->bearing.y * scale;

        // Bottom-left corner in pixel space
        const glm::vec2 bl = pixelPos + glm::vec2{cursorX + bx, -(by - gh)};

        SdfGlyphGPU gv{};
        gv.worldPos = {bl.x, bl.y, 0.f}; // pixel coords packed into worldPos.xy
        gv.size = gh;
        gv.uvMin = g->uvMin;
        gv.uvMax = g->uvMax;
        gv.color = color;
        gv.right = {1.f, 0.f, 0.f};
        gv._p0 = 0.f;
        gv.up = {0.f, 1.f, 0.f};
        gv._p1 = 0.f;
        (void)gw;

        hudGlyphs_.push_back(gv);
        cursorX += g->advance * scale;
    }
}
