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
    bool init(SDL_GPUDevice* dev, const char* ttfPath);
    void quit();

    /// @brief Queue world-space billboard text (faces camera, depth-tested).
    void drawWorldText(glm::vec3 worldPos,
                       std::string_view text,
                       glm::vec4 color,
                       float worldHeight,
                       glm::vec3 camRight,
                       glm::vec3 camUp);

    /// @brief Queue HUD text in pixel coordinates (no depth test).
    void drawScreenText(glm::vec2 pixelPos, std::string_view text, glm::vec4 color, float pixelHeight);

    /// @brief Clear queues (called at start of each frame).
    void clear();

    [[nodiscard]] const SdfGlyphGPU* worldData() const { return worldGlyphs_.data(); }
    [[nodiscard]] uint32_t worldCount() const { return static_cast<uint32_t>(worldGlyphs_.size()); }
    [[nodiscard]] const SdfGlyphGPU* hudData() const { return hudGlyphs_.data(); }
    [[nodiscard]] uint32_t hudCount() const { return static_cast<uint32_t>(hudGlyphs_.size()); }

    [[nodiscard]] bool ready() const { return atlas_.ready(); }

    // The SdfAtlas also provides the GPU texture binding used by the pipelines.
    [[nodiscard]] const SdfAtlas& atlas() const { return atlas_; }

private:
    SdfAtlas atlas_;
    std::vector<SdfGlyphGPU> worldGlyphs_;
    std::vector<SdfGlyphGPU> hudGlyphs_;
};
