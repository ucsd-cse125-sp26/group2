#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>

/// @brief Per-glyph metrics stored after SDF atlas bake.
struct GlyphInfo
{
    glm::vec2 uvMin{};   ///< Normalised atlas UV top-left.
    glm::vec2 uvMax{};   ///< Normalised atlas UV bottom-right.
    glm::vec2 bearing{}; ///< Offset from cursor baseline to glyph top-left (pixels at bake size).
    float advance{};     ///< Horizontal cursor advance (pixels at bake size).
    float width{};       ///< Glyph pixel width at bake size.
    float height{};      ///< Glyph pixel height at bake size.
};

/// @brief Immutable glyph metrics table.
using GlyphMap = std::unordered_map<uint32_t, GlyphInfo>;
