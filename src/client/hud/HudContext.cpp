/// @file HudContext.cpp
/// @brief Immediate-mode draw API implementation.

#include "HudContext.hpp"

#include "particles/sdf/SdfAtlas.hpp"
#include "particles/sdf/SdfFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

void HudContext::init(const SdfAtlas* atlas)
{
    sdfAtlas_ = atlas;
}

void HudContext::beginFrame()
{
    vertices_.clear();
    clipSpans_.clear();
    clipStack_.clear();
    spanStartVertex_ = 0;
    spanDirty_ = false;
}

void HudContext::endFrame()
{
    // Flush any remaining vertices into a final clip span so nothing is lost.
    if (spanDirty_)
        flushClipSpan();
}

// ── Internal helpers ────────────────────────────────────────────────────────

void HudContext::emitQuad(float x,
                          float y,
                          float w,
                          float h,
                          float u0,
                          float v0,
                          float u1,
                          float v1,
                          HudColor c,
                          float texMode,
                          float sd0,
                          float sd1,
                          float sd2)
{
    spanDirty_ = true;

    auto makeVtx = [&](float px, float py, float u, float v) -> HudVertex {
        return HudVertex{
            {px, py},
            {u, v},
            {c.r, c.g, c.b, c.a},
            texMode,
            {sd0, sd1, sd2},
        };
    };

    // Two triangles: TL-TR-BL, TR-BR-BL.
    vertices_.push_back(makeVtx(x, y, u0, v0));
    vertices_.push_back(makeVtx(x + w, y, u1, v0));
    vertices_.push_back(makeVtx(x, y + h, u0, v1));
    vertices_.push_back(makeVtx(x + w, y, u1, v0));
    vertices_.push_back(makeVtx(x + w, y + h, u1, v1));
    vertices_.push_back(makeVtx(x, y + h, u0, v1));
}

void HudContext::flushClipSpan()
{
    const uint32_t vertexCount = static_cast<uint32_t>(vertices_.size()) - spanStartVertex_;
    if (vertexCount == 0)
        return;

    std::array<float, 6> span{};
    span[0] = static_cast<float>(spanStartVertex_);
    span[1] = static_cast<float>(vertexCount);

    if (clipStack_.empty()) {
        span[2] = 0.f;
        span[3] = 0.f;
        span[4] = -1.f; // Negative w = no scissor.
        span[5] = 0.f;
    } else {
        const auto& cr = clipStack_.back();
        span[2] = cr[0];
        span[3] = cr[1];
        span[4] = cr[2];
        span[5] = cr[3];
    }

    clipSpans_.push_back(span);
    spanStartVertex_ = static_cast<uint32_t>(vertices_.size());
    spanDirty_ = false;
}

// ── Primitives ──────────────────────────────────────────────────────────────

void HudContext::rect(float x, float y, float w, float h, HudColor color)
{
    emitQuad(x, y, w, h, 0, 0, 0, 0, color, 0.f);
}

void HudContext::rotatedRect(float cx, float cy, float w, float h, float angleDeg, HudColor color)
{
    const float rad = angleDeg * 3.14159265f / 180.f;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;

    // Four corners relative to center, rotated.
    auto rot = [&](float lx, float ly) -> std::pair<float, float> {
        return {cx + lx * cosA - ly * sinA, cy + lx * sinA + ly * cosA};
    };

    auto [tlx, tly] = rot(-hw, -hh);
    auto [trx, trY] = rot(hw, -hh);
    auto [brx, brY] = rot(hw, hh);
    auto [blx, blY] = rot(-hw, hh);

    spanDirty_ = true;
    auto v = [&](float px, float py) -> HudVertex {
        return HudVertex{{px, py}, {0, 0}, {color.r, color.g, color.b, color.a}, 0.f, {0, 0, 0}};
    };

    // Two triangles: TL-TR-BL, TR-BR-BL.
    vertices_.push_back(v(tlx, tly));
    vertices_.push_back(v(trx, trY));
    vertices_.push_back(v(blx, blY));
    vertices_.push_back(v(trx, trY));
    vertices_.push_back(v(brx, brY));
    vertices_.push_back(v(blx, blY));
}

void HudContext::rectOutline(float x, float y, float w, float h, float thickness, HudColor color)
{
    rect(x, y, w, thickness, color);                                             // top
    rect(x, y + h - thickness, w, thickness, color);                             // bottom
    rect(x, y + thickness, thickness, h - 2 * thickness, color);                 // left
    rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color); // right
}

void HudContext::triangle(float x0, float y0, float x1, float y1, float x2, float y2, HudColor c)
{
    spanDirty_ = true;
    auto v = [&](float px, float py) -> HudVertex {
        return HudVertex{{px, py}, {0.f, 0.f}, {c.r, c.g, c.b, c.a}, 0.f, {0, 0, 0}};
    };
    vertices_.push_back(v(x0, y0));
    vertices_.push_back(v(x1, y1));
    vertices_.push_back(v(x2, y2));
}

void HudContext::polyline(const float* points, int numPoints, float thickness, HudColor color)
{
    if (!points || numPoints < 2)
        return;
    for (int i = 0; i < numPoints - 1; ++i) {
        const float x0 = points[i * 2 + 0];
        const float y0 = points[i * 2 + 1];
        const float x1 = points[i * 2 + 2];
        const float y1 = points[i * 2 + 3];
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f)
            continue;
        const float angDeg = std::atan2(dy, dx) * 180.f / 3.14159265f + 90.f;
        const float cx = (x0 + x1) * 0.5f;
        const float cy = (y0 + y1) * 0.5f;
        rotatedRect(cx, cy, thickness, len, angDeg, color);
    }
}

void HudContext::roundedRect(float x, float y, float w, float h, float radius, HudColor color)
{
    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;
    emitQuad(x,
             y,
             w,
             h,
             0.f,
             0.f,
             1.f,
             1.f, // UV: [0,1] for SDF shape local coords
             color,
             3.f, // texMode = 3
             halfW,
             halfH,
             radius);
}

// ── Bars ────────────────────────────────────────────────────────────────────

void HudContext::bar(float x, float y, float w, float h, float fill01, HudColor fg, HudColor bg)
{
    const float clampedFill = std::clamp(fill01, 0.f, 1.f);
    rect(x, y, w, h, bg);
    if (clampedFill > 0.f)
        rect(x, y, w * clampedFill, h, fg);
}

// ── Text ────────────────────────────────────────────────────────────────────

void HudContext::text(const char* str, float x, float y, float size, HudColor color, HudAlign align)
{
    if (!sdfAtlas_ || !str || !*str)
        return;

    const float scale = size / static_cast<float>(SdfAtlas::k_renderPx);

    // Measure for alignment.
    float totalWidth = 0.f;
    if (align != HudAlign::Left)
        totalWidth = measureText(str, size);

    float startX = x;
    if (align == HudAlign::Center)
        startX = x - totalWidth * 0.5f;
    else if (align == HudAlign::Right)
        startX = x - totalWidth;

    // Pixel-snap the starting cursor and the baseline so glyphs land on a
    // consistent subpixel grid (eliminates the "100" / "111" sampling drift
    // where identical zero-glyphs render at slightly different widths).
    float cursorX = std::round(startX);
    const float baselineY = std::round(y + size);

    // Per-glyph cursor accumulation is the source of the inter-glyph jitter
    // in the original implementation: a fractional `advance * scale` would
    // compound, and rounding *only* the position made some glyphs land at
    // an 8-px gap and others at 9-px, even when the source glyphs were
    // identical.  We track the cursor in float for sub-pixel accuracy of
    // the whole string but compute each glyph's quad anchor as the
    // round-to-nearest of `cursorX + bearing*scale`, then advance the
    // cursor by an integer-rounded delta so identical successor glyphs
    // step by exactly the same number of pixels.
    for (const char* p = str; *p; ++p) {
        const uint32_t cp = static_cast<uint8_t>(*p);
        const GlyphInfo* gi = sdfAtlas_->glyph(cp);
        if (!gi)
            continue;

        const float gw = gi->width * scale;
        const float gh = gi->height * scale;
        if (gw > 0.f && gh > 0.f) {
            const float gx = std::round(cursorX + gi->bearing.x * scale);
            const float gy = std::round(baselineY - gi->bearing.y * scale);
            emitQuad(gx, gy, gw, gh, gi->uvMin.x, gi->uvMin.y, gi->uvMax.x, gi->uvMax.y, color, 1.f);
        }

        // Round the per-glyph advance so successive identical glyphs step
        // by the same integer number of pixels, killing the spacing jitter.
        cursorX += std::round(gi->advance * scale);
    }
}

float HudContext::measureText(const char* str, float size) const
{
    if (!sdfAtlas_ || !str || !*str)
        return 0.f;

    const float scale = size / static_cast<float>(SdfAtlas::k_renderPx);
    float width = 0.f;
    for (const char* p = str; *p; ++p) {
        const GlyphInfo* gi = sdfAtlas_->glyph(static_cast<uint8_t>(*p));
        if (gi)
            width += gi->advance * scale;
    }
    return width;
}

// ── Icons ───────────────────────────────────────────────────────────────────

void HudContext::icon(HudIcon /*id*/, float x, float y, float size, HudColor tint)
{
    // TODO: look up icon UV rect from atlas by id.  For now, full 1x1 fallback.
    emitQuad(x, y, size, size, 0.f, 0.f, 1.f, 1.f, tint, 2.f);
}

// ── Crosshair ───────────────────────────────────────────────────────────────

void HudContext::crosshair(const CrosshairStyle& style, float screenW, float screenH)
{
    const float cx = screenW * 0.5f;
    const float cy = screenH * 0.5f;
    const float gap = style.gap;
    const float len = style.length;
    const float t = style.thickness;
    const float ht = t * 0.5f;

    // Four arms.
    rect(cx + gap, cy - ht, len, t, style.color);       // right
    rect(cx - gap - len, cy - ht, len, t, style.color); // left
    rect(cx - ht, cy - gap - len, t, len, style.color); // top
    rect(cx - ht, cy + gap, t, len, style.color);       // bottom

    // Center dot.
    if (style.dot)
        rect(cx - ht, cy - ht, t, t, style.color);
}

// ── Vignette ────────────────────────────────────────────────────────

void HudContext::vignette(float screenW, float screenH, HudColor color)
{
    // Full-screen quad with texMode=4 (radial edge gradient in fragment shader).
    emitQuad(0.f, 0.f, screenW, screenH, 0.f, 0.f, 1.f, 1.f, color, 4.f);
}

// ── Clipping ────────────────────────────────────────────────────────────────

void HudContext::pushClipRect(float x, float y, float w, float h)
{
    if (spanDirty_)
        flushClipSpan();
    clipStack_.push_back({x, y, w, h});
}

void HudContext::popClipRect()
{
    if (spanDirty_)
        flushClipSpan();
    if (!clipStack_.empty())
        clipStack_.pop_back();
}
