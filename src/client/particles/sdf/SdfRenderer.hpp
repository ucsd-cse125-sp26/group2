/// @file SdfRenderer.hpp
/// @brief High-level SDF text renderer for world-space and screen-space text.

#pragma once

#include "SdfAtlas.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <string_view>
#include <vector>

/// @brief Queues SDF glyph quads for world-space and screen-space text.
///
/// Call drawWorldText() / drawScreenText() each frame to queue text.
/// The ParticleSystem flushes the queues during uploadToGpu() / render().
class SdfRenderer
{
public:
    /// @brief Initialise the renderer by loading a font and baking the SDF atlas.
    /// @param dev GPU device used for atlas texture creation.
    /// @param ttfPath Filesystem path to the TTF font file.
    /// @return true on success.
    bool init(SDL_GPUDevice* dev, const char* ttfPath);

    /// @brief Release all resources owned by this renderer.
    void quit();

    /// @brief Queue world-space billboard text (faces camera, depth-tested).
    /// @param worldPos World-space origin of the text baseline.
    /// @param text UTF-8 text string to render.
    /// @param color RGBA colour for all glyphs.
    /// @param worldHeight Desired text height in world units.
    /// @param camRight Camera right vector for billboarding.
    /// @param camUp Camera up vector for billboarding.
    void drawWorldText(glm::vec3 worldPos,
                       std::string_view text,
                       glm::vec4 color,
                       float worldHeight,
                       glm::vec3 camRight,
                       glm::vec3 camUp);

    /// @brief Queue HUD text in pixel coordinates (no depth test).
    /// @param pixelPos Screen-space position in pixels.
    /// @param text UTF-8 text string to render.
    /// @param color RGBA colour for all glyphs.
    /// @param pixelHeight Desired text height in pixels.
    void drawScreenText(glm::vec2 pixelPos, std::string_view text, glm::vec4 color, float pixelHeight);

    /// @brief Clear queues (called at start of each frame).
    void clear();

    /// @brief Get a pointer to the world-space glyph data buffer.
    /// @return Pointer to the first world glyph, or nullptr if empty.
    [[nodiscard]] const SdfGlyphGPU* worldData() const { return worldGlyphs_.data(); }

    /// @brief Get the number of queued world-space glyphs.
    /// @return World glyph count.
    [[nodiscard]] uint32_t worldCount() const { return static_cast<uint32_t>(worldGlyphs_.size()); }

    /// @brief Get a pointer to the HUD glyph data buffer.
    /// @return Pointer to the first HUD glyph, or nullptr if empty.
    [[nodiscard]] const SdfGlyphGPU* hudData() const { return hudGlyphs_.data(); }

    /// @brief Get the number of queued HUD glyphs.
    /// @return HUD glyph count.
    [[nodiscard]] uint32_t hudCount() const { return static_cast<uint32_t>(hudGlyphs_.size()); }

    /// @brief Check whether the underlying atlas is ready for rendering.
    /// @return true if the atlas texture is valid.
    [[nodiscard]] bool ready() const { return atlas_.ready(); }

    /// @brief Access the underlying SDF atlas (provides GPU texture binding).
    /// @return Const reference to the SdfAtlas.
    [[nodiscard]] const SdfAtlas& atlas() const { return atlas_; }

private:
    SdfAtlas atlas_;
    std::vector<SdfGlyphGPU> worldGlyphs_;
    std::vector<SdfGlyphGPU> hudGlyphs_;
};
