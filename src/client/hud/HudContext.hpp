/// @file HudContext.hpp
/// @brief Immediate-mode draw API for HUD widgets.

#pragma once

#include "HudTypes.hpp"

#include <array>
#include <vector>

class SdfAtlas;

/// @brief Accumulates HUD geometry during a frame for batch rendering.
///
/// Widgets call rect(), text(), bar(), etc. to emit quads.  At the end of
/// the frame, HudRenderer consumes the vertex buffer and clip rect list.
class HudContext
{
public:
    /// @brief Bind the SDF atlas for text layout (glyph metrics).
    void init(const SdfAtlas* atlas);

    /// @brief Clear all geometry for a new frame.
    void beginFrame();

    // ── Primitives ──────────────────────────────────────────────────────

    void rect(float x, float y, float w, float h, HudColor color);
    void rectOutline(float x, float y, float w, float h, float thickness, HudColor color);
    void roundedRect(float x, float y, float w, float h, float radius, HudColor color);
    void rotatedRect(float cx, float cy, float w, float h, float angleDeg, HudColor color);

    // ── Bars ────────────────────────────────────────────────────────────

    void bar(float x, float y, float w, float h, float fill01, HudColor fg, HudColor bg);

    // ── Text ────────────────────────────────────────────────────────────

    void text(const char* str, float x, float y, float size, HudColor color, HudAlign align = HudAlign::Left);
    float measureText(const char* str, float size) const;

    // ── Icons ───────────────────────────────────────────────────────────

    void icon(HudIcon id, float x, float y, float size, HudColor tint = HudColor::white());

    // ── Crosshair ───────────────────────────────────────────────────────

    void crosshair(const CrosshairStyle& style, float screenW, float screenH);

    // ── Clipping ────────────────────────────────────────────────────────

    void pushClipRect(float x, float y, float w, float h);
    void popClipRect();

    // ── Access for HudRenderer ──────────────────────────────────────────

    [[nodiscard]] const std::vector<HudVertex>& vertices() const { return vertices_; }

    /// @brief Clip rect spans: {startVertex, vertexCount, x, y, w, h}.
    /// Negative w means "full viewport" (no scissor).
    [[nodiscard]] const std::vector<std::array<float, 6>>& clipSpans() const { return clipSpans_; }

private:
    const SdfAtlas* sdfAtlas_ = nullptr;
    std::vector<HudVertex> vertices_;
    std::vector<std::array<float, 6>> clipSpans_;

    // Clip stack: each entry is {x, y, w, h}.  Empty = no clip.
    std::vector<std::array<float, 4>> clipStack_;
    uint32_t spanStartVertex_ = 0; ///< Vertex index where current clip span started.
    bool spanDirty_ = false;

    /// @brief Emit 6 vertices for a textured quad.
    void emitQuad(float x,
                  float y,
                  float w,
                  float h,
                  float u0,
                  float v0,
                  float u1,
                  float v1,
                  HudColor color,
                  float texMode,
                  float sd0 = 0.f,
                  float sd1 = 0.f,
                  float sd2 = 0.f);

    /// @brief Flush the current clip span before changing scissor state.
    void flushClipSpan();
};
