/// @file HudContext.hpp
/// @brief Immediate-mode draw API for HUD widgets.

#pragma once

#include "HudTypes.hpp"

#include <array>
#include <vector>

class SdfAtlas;
class HudSvgAtlas;

/// @brief Accumulates HUD geometry during a frame for batch rendering.
///
/// Widgets call rect(), text(), bar(), etc. to emit quads.  At the end of
/// the frame, HudRenderer consumes the vertex buffer and clip rect list.
class HudContext
{
public:
    /// @brief Bind the SDF atlas for text layout (glyph metrics).
    void init(const SdfAtlas* atlas, HudSvgAtlas* svgAtlas = nullptr);

    /// @brief Clear all geometry for a new frame.
    void beginFrame();

    // ── Primitives ──────────────────────────────────────────────────────

    void rect(float x, float y, float w, float h, HudColor color);
    void rectOutline(float x, float y, float w, float h, float thickness, HudColor color);
    void roundedRect(float x, float y, float w, float h, float radius, HudColor color);
    void rotatedRect(float cx, float cy, float w, float h, float angleDeg, HudColor color);

    /// @brief Filled rect with a horizontal color gradient.
    ///
    /// `leftColor` is at the left edge, `rightColor` at the right.  The
    /// rasteriser perspective-correct-interpolates per-vertex colors across
    /// the quad, so the gradient is exact at any size and any rotation
    /// (though we only emit axis-aligned gradients here — the design's
    /// HP / shield bars are horizontal).
    void gradientRect(float x, float y, float w, float h, HudColor leftColor, HudColor rightColor);

    /// @brief Emit a single solid-colored triangle (3 vertices).
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, HudColor color);

    /// @brief Triangle with per-vertex colors — the rasteriser interpolates
    ///        across the surface.  Used by chamfered / pentagon panels that
    ///        need a horizontal or radial gradient.
    void
    triangleColors(float x0, float y0, HudColor c0, float x1, float y1, HudColor c1, float x2, float y2, HudColor c2);

    /// @brief Stroke a polyline with sharp-mitred corners using rotated rects.
    /// @param points 2*N pairs (x0,y0,x1,y1,...) — N >= 2.
    /// @param numPoints Number of (x,y) points.
    /// @param thickness Line width in pixels.
    /// @param color    Stroke color.
    void polyline(const float* points, int numPoints, float thickness, HudColor color);

    // ── Bars ────────────────────────────────────────────────────────────

    void bar(float x, float y, float w, float h, float fill01, HudColor fg, HudColor bg);

    // ── Text ────────────────────────────────────────────────────────────

    /// @brief Render a UTF-8 ASCII string via SDF.
    /// @param outlined  When true the glyph picks up a 1-px dark outline for
    ///                  legibility against bright/varied backgrounds (damage
    ///                  numbers over the world, etc.).  Defaults to off so
    ///                  text rendered onto the dark Voidfall panel chrome
    ///                  stays clean and doesn't gain a "sticker" look.
    void text(const char* str,
              float x,
              float y,
              float size,
              HudColor color,
              HudAlign align = HudAlign::Left,
              bool outlined = false);
    void knockoutText(const char* str, float x, float y, float size, HudAlign align = HudAlign::Left);
    float measureText(const char* str, float size) const;

    // ── Icons ───────────────────────────────────────────────────────────

    bool icon(HudIcon id, float x, float y, float size, HudColor tint = HudColor::white());
    bool svg(HudIcon id, float x, float y, float w, float h, HudColor tint = HudColor::white());
    bool svgFlipped(HudIcon id, float x, float y, float w, float h, bool flipX, bool flipY, HudColor tint = HudColor::white());
    bool svgMask(HudIcon id, float x, float y, float w, float h, HudColor color);
    bool svgMaskFlipped(HudIcon id, float x, float y, float w, float h, bool flipX, bool flipY, HudColor color);

    // ── Crosshair ───────────────────────────────────────────────────────

    void crosshair(const CrosshairStyle& style, float screenW, float screenH);

    // ── Vignette ────────────────────────────────────────────────────────

    /// @brief Draw a full-screen radial vignette overlay.
    /// @param screenW  Viewport width in pixels.
    /// @param screenH  Viewport height in pixels.
    /// @param color    Tint color with alpha controlling intensity.
    void vignette(float screenW, float screenH, HudColor color);

    /// @brief Draw a full-screen scope mask with a transparent circular cut-out.
    /// @param screenW Viewport width in pixels.
    /// @param screenH Viewport height in pixels.
    /// @param radiusPx Radius of the clear scope glass in pixels.
    /// @param color Mask tint; alpha controls opacity outside the cut-out.
    void scopeMask(float screenW, float screenH, float radiusPx, HudColor color);

    // ── Clipping ────────────────────────────────────────────────────────

    void pushClipRect(float x, float y, float w, float h);
    void popClipRect();

    /// @brief Flush any remaining unflushed vertices into a final clip span.
    /// Must be called after all draw() calls, before accessing vertices/clipSpans.
    void endFrame();

    /// @brief Multiply already-emitted vertex colors by a tint.
    void tintVertices(std::size_t startVertex, HudColor tint);

    // ── Access for HudRenderer ──────────────────────────────────────────

    [[nodiscard]] const std::vector<HudVertex>& vertices() const { return vertices_; }

    /// @brief Clip rect spans: {startVertex, vertexCount, x, y, w, h}.
    /// Negative w means "full viewport" (no scissor).
    [[nodiscard]] const std::vector<std::array<float, 6>>& clipSpans() const { return clipSpans_; }

private:
    const SdfAtlas* sdfAtlas_ = nullptr;
    HudSvgAtlas* svgAtlas_ = nullptr;
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
