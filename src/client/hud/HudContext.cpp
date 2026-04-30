/// @file HudContext.cpp
/// @brief Immediate-mode draw API implementation.

#include "HudContext.hpp"

#include "particles/sdf/SdfAtlas.hpp"
#include "particles/sdf/SdfFont.hpp"

#include <algorithm>
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

void HudContext::rectOutline(float x, float y, float w, float h, float thickness, HudColor color)
{
    rect(x, y, w, thickness, color);                                             // top
    rect(x, y + h - thickness, w, thickness, color);                             // bottom
    rect(x, y + thickness, thickness, h - 2 * thickness, color);                 // left
    rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color); // right
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

    float cursorX = startX;
    for (const char* p = str; *p; ++p) {
        const uint32_t cp = static_cast<uint32_t>(*p);
        const GlyphInfo* gi = sdfAtlas_->glyph(cp);
        if (!gi)
            continue;

        const float gw = gi->width * scale;
        const float gh = gi->height * scale;
        const float gx = cursorX + gi->bearing.x * scale;
        const float gy = y - gi->bearing.y * scale + size; // baseline offset

        emitQuad(
            gx, gy, gw, gh, gi->uvMin.x, gi->uvMin.y, gi->uvMax.x, gi->uvMax.y, color, 1.f); // texMode = 1 (SDF text)

        cursorX += gi->advance * scale;
    }
}

float HudContext::measureText(const char* str, float size) const
{
    if (!sdfAtlas_ || !str || !*str)
        return 0.f;

    const float scale = size / static_cast<float>(SdfAtlas::k_renderPx);
    float width = 0.f;
    for (const char* p = str; *p; ++p) {
        const GlyphInfo* gi = sdfAtlas_->glyph(static_cast<uint32_t>(*p));
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
