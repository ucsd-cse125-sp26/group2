/// @file HudIcons.cpp
/// @brief Procedural vector icon implementations.

#include "HudIcons.hpp"

#include "HudContext.hpp"
#include "VoidfallStyle.hpp"

#include <cmath>
#include <numbers>
#include <vector>

namespace voidfall::icons
{

namespace
{

constexpr float k_pi = std::numbers::pi_v<float>;

/// @brief Convenience: degrees → radians.
constexpr float deg(float d)
{
    return d * (k_pi / 180.f);
}

} // namespace

// ── Shared primitives ──────────────────────────────────────────────────────

void filledPolygon(HudContext& ctx, const float* points, int n, HudColor color)
{
    if (n < 3)
        return;
    // Triangle fan from points[0].
    const float x0 = points[0];
    const float y0 = points[1];
    for (int i = 1; i < n - 1; ++i) {
        const float x1 = points[i * 2 + 0];
        const float y1 = points[i * 2 + 1];
        const float x2 = points[i * 2 + 2];
        const float y2 = points[i * 2 + 3];
        ctx.triangle(x0, y0, x1, y1, x2, y2, color);
    }
}

void strokedPolygon(HudContext& ctx, const float* points, int n, float thickness, HudColor color)
{
    if (n < 2)
        return;
    // Re-pack with the first point appended at the end so polyline closes
    // the loop.  Avoid heap alloc for small polygons (typical icon ≤ 16 vtx).
    constexpr int k_stack = 32;
    float stack[k_stack * 2];
    std::vector<float> heap;
    float* buf = stack;
    if (n + 1 > k_stack) {
        heap.resize(static_cast<std::size_t>(n + 1) * 2u);
        buf = heap.data();
    }
    for (int i = 0; i < n; ++i) {
        buf[i * 2 + 0] = points[i * 2 + 0];
        buf[i * 2 + 1] = points[i * 2 + 1];
    }
    buf[n * 2 + 0] = points[0];
    buf[n * 2 + 1] = points[1];
    ctx.polyline(buf, n + 1, thickness, color);
}

void filledCircle(HudContext& ctx, float cx, float cy, float r, int sides, HudColor color)
{
    if (sides < 3 || r <= 0.f)
        return;
    float prevX = cx + r;
    float prevY = cy;
    for (int i = 1; i <= sides; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(sides)) * (2.f * k_pi);
        const float nx = cx + std::cos(a) * r;
        const float ny = cy + std::sin(a) * r;
        ctx.triangle(cx, cy, prevX, prevY, nx, ny, color);
        prevX = nx;
        prevY = ny;
    }
}

void strokedCircle(HudContext& ctx, float cx, float cy, float r, float thickness, int sides, HudColor color)
{
    if (sides < 3 || r <= 0.f)
        return;
    constexpr int k_stack = 64;
    float stack[k_stack * 2];
    std::vector<float> heap;
    float* buf = stack;
    const int total = sides + 1;
    if (total > k_stack) {
        heap.resize(static_cast<std::size_t>(total) * 2u);
        buf = heap.data();
    }
    for (int i = 0; i <= sides; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(sides)) * (2.f * k_pi);
        buf[i * 2 + 0] = cx + std::cos(a) * r;
        buf[i * 2 + 1] = cy + std::sin(a) * r;
    }
    ctx.polyline(buf, total, thickness, color);
}

// ── Glyphs ────────────────────────────────────────────────────────────────

void hp(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Hp, x, y, size, color))
        return;

    // Medical cross: a + shape, filled.  Two rectangles forming a plus.
    const float u = size / 14.f;
    // Vertical bar.
    ctx.rect(x + 5.f * u, y + 2.f * u, 4.f * u, 10.f * u, color);
    // Horizontal bar.
    ctx.rect(x + 2.f * u, y + 5.f * u, 10.f * u, 4.f * u, color);
}

void shield(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Shield, x, y, size, color))
        return;

    // Heater-shield silhouette: 6-vertex polygon stroked at 1.4 px.
    // Approximates `M7 1 L12 3 V7 C12 10 9.5 12 7 13 C4.5 12 2 10 2 7 V3 Z`
    // by sampling the curve into linear segments — the eye won't tell at 14 px.
    const float u = size / 14.f;
    const float pts[] = {
        x + 7.0f * u,  y + 1.0f * u,  // top center
        x + 12.0f * u, y + 3.0f * u,  // top-right shoulder
        x + 12.0f * u, y + 7.0f * u,  // right edge
        x + 11.0f * u, y + 9.5f * u,  // upper-right curve
        x + 9.0f * u,  y + 12.0f * u, // lower-right curve
        x + 7.0f * u,  y + 13.0f * u, // bottom point
        x + 5.0f * u,  y + 12.0f * u, // lower-left curve
        x + 3.0f * u,  y + 9.5f * u,  // upper-left curve
        x + 2.0f * u,  y + 7.0f * u,  // left edge
        x + 2.0f * u,  y + 3.0f * u,  // top-left shoulder
    };
    constexpr int n = sizeof(pts) / (sizeof(float) * 2);
    strokedPolygon(ctx, pts, n, 1.4f * (u < 1.f ? 1.f : u), color);
}

void skull(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Skull, x, y, size, color))
        return;

    // Simple stylised skull — round top, flat bottom with two notches.
    const float u = size / 12.f;
    // Cranium: filled circle approximation.
    filledCircle(ctx, x + 6.f * u, y + 5.f * u, 3.5f * u, 14, color);
    // Jaw: two rects with a gap between them.
    ctx.rect(x + 3.f * u, y + 7.5f * u, 2.5f * u, 2.5f * u, color);
    ctx.rect(x + 6.5f * u, y + 7.5f * u, 2.5f * u, 2.5f * u, color);
    // Eye sockets (knocked out as small black squares).
    ctx.rect(x + 4.f * u, y + 5.f * u, 1.5f * u, 1.5f * u, withAlpha(k_quaternary, 0.85f));
    ctx.rect(x + 6.5f * u, y + 5.f * u, 1.5f * u, 1.5f * u, withAlpha(k_quaternary, 0.85f));
}

void headshot(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Headshot, x, y, size, color))
        return;

    // Reticle: circle outline + center pip + tick below.
    const float u = size / 12.f;
    const float cx = x + 6.f * u;
    const float cy = y + 5.f * u;
    strokedCircle(ctx, cx, cy, 3.f * u, 1.2f * u, 16, color);
    ctx.rect(cx - 0.4f * u, cy - 0.4f * u, 0.8f * u, 0.8f * u, color);
    ctx.rect(cx - 0.4f * u, cy + 4.f * u, 0.8f * u, 2.f * u, color);
}

void gravityArrow(HudContext& ctx, float x, float y, float size, int direction, HudColor color)
{
    const float cx = x + size * 0.5f;
    const float cy = y + size * 0.5f;
    const float angle = static_cast<float>(direction) * 90.f;

    // Arrow body in local frame (pointing down).
    const float bodyHalfLen = size * 0.30f;
    const float bodyThk = size * 0.06f;
    ctx.rotatedRect(cx, cy, bodyThk, bodyHalfLen * 2.f, angle, color);

    // Arrowhead: filled triangle pointing in arrow direction.
    const float tipDist = bodyHalfLen + size * 0.05f;
    const float baseDist = bodyHalfLen - size * 0.10f;
    const float baseHalf = size * 0.18f;

    // Local-frame tip and base, then rotate.
    const float rad = deg(angle);
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    auto rot = [&](float lx, float ly) -> std::pair<float, float> {
        return {cx + lx * c - ly * s, cy + lx * s + ly * c};
    };
    auto [tx, ty] = rot(0.f, tipDist);
    auto [b0x, b0y] = rot(-baseHalf, baseDist);
    auto [b1x, b1y] = rot(baseHalf, baseDist);
    ctx.triangle(tx, ty, b0x, b0y, b1x, b1y, color);
}

void fall(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Fall, x, y, size, color))
        return;

    const float u = size / 12.f;
    const float pts[] = {
        x + 6.f * u,
        y + 2.f * u,
        x + 10.f * u,
        y + 10.f * u,
        x + 2.f * u,
        y + 10.f * u,
    };
    filledPolygon(ctx, pts, 3, color);
}

void grenade(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Grenade, x, y, size, color))
        return;

    const float u = size / 14.f;
    // Round body.
    filledCircle(ctx, x + 7.f * u, y + 9.f * u, 4.f * u, 18, color);
    // Collar (top of body).
    ctx.rect(x + 5.5f * u, y + 3.f * u, 3.f * u, 2.5f * u, color);
    // Spoon/lever angled out to the right.
    ctx.rotatedRect(x + 9.5f * u, y + 3.0f * u, 1.4f * u, 2.5f * u, 30.f, color);
    // Pin (small circle at top).
    filledCircle(ctx, x + 8.5f * u, y + 2.f * u, 0.8f * u, 8, color);
}

void grapple(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Grapple, x, y, size, color))
        return;

    // Three strokes: rope (diagonal), hook crossbar, hook shaft + prongs.
    const float u = size / 14.f;
    const float t = 1.4f * u;

    // Rope from bottom-left to mid.
    const float rope[] = {
        x + 1.f * u,
        y + 13.f * u,
        x + 7.f * u,
        y + 7.f * u,
    };
    ctx.polyline(rope, 2, t, color);

    // Crossbar.
    const float bar[] = {
        x + 4.f * u,
        y + 4.f * u,
        x + 10.f * u,
        y + 4.f * u,
    };
    ctx.polyline(bar, 2, t, color);

    // Shaft straight down from crossbar to mid.
    const float shaft[] = {
        x + 7.f * u,
        y + 4.f * u,
        x + 7.f * u,
        y + 7.f * u,
    };
    ctx.polyline(shaft, 2, t, color);

    // Two prongs angling out from mid.
    const float prongL[] = {
        x + 7.f * u,
        y + 7.f * u,
        x + 5.f * u,
        y + 5.5f * u,
    };
    ctx.polyline(prongL, 2, t, color);
    const float prongR[] = {
        x + 7.f * u,
        y + 7.f * u,
        x + 9.f * u,
        y + 5.5f * u,
    };
    ctx.polyline(prongR, 2, t, color);
}

void tactical(HudContext& ctx, float x, float y, float size, HudColor color)
{
    if (ctx.icon(HudIcon::Tactical, x, y, size, color))
        return;

    const float u = size / 14.f;
    const float cx = x + 7.f * u;
    const float cy = y + 7.f * u;
    strokedCircle(ctx, cx, cy, 5.f * u, 1.2f * u, 18, color);
    filledCircle(ctx, cx, cy, 1.5f * u, 10, color);
}

void playerArrow(HudContext& ctx, float cx, float cy, float size, HudColor color)
{
    if (ctx.icon(HudIcon::PlayerArrow, cx - size * 0.5f, cy - size * 0.5f, size, color))
        return;

    // Filled chevron with a notch — matches design's `M0,-8 L6,6 L0,3 L-6,6 Z`.
    const float h = size * 0.5f;
    const float halfW = size * 0.4f;
    const float baseY = cy + h * 0.6f;
    const float notchY = cy + (h * 0.6f - size * 0.18f);
    const float tipY = cy - h;
    ctx.triangle(cx, tipY, cx - halfW, baseY, cx, notchY, color);
    ctx.triangle(cx, tipY, cx, notchY, cx + halfW, baseY, color);
}

void enemyDiamond(HudContext& ctx, float cx, float cy, float size, HudColor color)
{
    if (ctx.icon(HudIcon::EnemyDiamond, cx - size * 0.5f, cy - size * 0.5f, size, color))
        return;

    const float h = size * 0.5f;
    ctx.triangle(cx, cy - h, cx + h, cy, cx, cy + h, color);
    ctx.triangle(cx, cy - h, cx, cy + h, cx - h, cy, color);
}

} // namespace voidfall::icons
