/// @file Minimap.cpp
/// @brief Prototype circular radar with polar grid and hologram ring.

#include "Minimap.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace
{
void drawCircleOutline(HudContext& ctx, float cx, float cy, float radius, float thickness, HudColor color)
{
    constexpr int kSegments = 64;
    float points[(kSegments + 1) * 2]{};
    for (int i = 0; i <= kSegments; ++i) {
        const float a = (static_cast<float>(i) / kSegments) * 2.f * 3.14159265f;
        points[i * 2 + 0] = cx + std::cos(a) * radius;
        points[i * 2 + 1] = cy + std::sin(a) * radius;
    }
    ctx.polyline(points, kSegments + 1, thickness, color);
}

void drawClockwiseRingArc(
    HudContext& ctx, float cx, float cy, float radius, float progress, float thickness, HudColor color)
{
    const float clampedProgress = std::clamp(progress, 0.f, 1.f);
    if (clampedProgress <= 0.f)
        return;

    constexpr int kMaxSegments = 96;
    const int segments = std::max(2, static_cast<int>(std::ceil(kMaxSegments * clampedProgress)));
    float points[(kMaxSegments + 1) * 2]{};
    for (int i = 0; i <= segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * clampedProgress;
        const float a = -0.5f * 3.14159265f + t * 2.f * 3.14159265f;
        points[i * 2 + 0] = cx + std::cos(a) * radius;
        points[i * 2 + 1] = cy + std::sin(a) * radius;
    }
    ctx.polyline(points, segments + 1, thickness, color);
}
} // namespace

Minimap::Minimap()
{
    anchor = HudAnchor::BottomLeft;
    offsetX = 65.f;
    offsetY = -410.f;
    width = 360.f;
    height = 360.f;
    mapSize = 360.f;
    dotSize = 12.f;
    borderThickness = 2.f;
}

void Minimap::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    localX_ = state.localPlayerX;
    localZ_ = state.localPlayerZ;
    localYaw_ = state.localPlayerYaw;
    worldRange_ = state.minimapWorldRange;

    const float targetLevel = std::clamp(state.abilityLevelProgress, 0.f, 1.f);
    const float liveLerp = std::clamp(dt * 12.f, 0.f, 1.f);
    liveLevel_ += (targetLevel - liveLevel_) * liveLerp;
    trailLevel_ = std::max(trailLevel_, targetLevel);
    if (trailLevel_ > liveLevel_) {
        const float drainSpeed = 1.f / std::max(0.05f, levelRingDrainSeconds);
        trailLevel_ = std::max(liveLevel_, trailLevel_ - drainSpeed * dt);
    } else {
        trailLevel_ = liveLevel_;
    }

    enemies_.clear();
    for (const auto& d : state.enemyDots)
        enemies_.push_back({d.worldX, d.worldZ});
}

void Minimap::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float ms = mapSize * s;
    const float cx = x + ms * 0.5f;
    const float cy = y + ms * 0.5f;
    const float radius = ms * 0.5f;
    const float ringT = std::max(0.f, levelRingThickness * s);
    const float ringRadius = radius + levelRingGap * s + ringT * 0.5f;

    // Frame.
    if (ringT > 0.f) {
        drawCircleOutline(ctx, cx, cy, ringRadius + 3.f * s, std::max(1.f, ringT * 1.3f), withAlpha(k_primary, 0.10f));
        drawCircleOutline(ctx, cx, cy, ringRadius, ringT, withAlpha(k_textDim, 0.28f));
        if (trailLevel_ > liveLevel_)
            drawClockwiseRingArc(ctx, cx, cy, ringRadius, trailLevel_, ringT, withAlpha(k_amberDim, 0.65f));
        drawClockwiseRingArc(ctx, cx, cy, ringRadius, liveLevel_, ringT, withAlpha(k_primary, 0.80f));
    }

    ctx.roundedRect(x, y, ms, ms, radius, HudColor{0.07f, 0.13f, 0.15f, 0.65f});
    const float borderT = std::max(0.f, borderThickness * s);
    if (borderT > 0.f)
        drawCircleOutline(ctx, cx, cy, std::max(0.f, radius - borderT * 0.5f), borderT, withAlpha(k_textDim, 0.48f));

    // Polar grid clipped to circular chords.
    const HudColor grid = HudColor{0.2745f, 0.4706f, 0.5333f, 0.25f};
    const float lineT = std::max(1.f, 1.f * s);
    const float gridRadius = std::max(0.f, radius - borderT - lineT * 0.5f);
    for (int i = 1; i <= 4; ++i)
        drawCircleOutline(ctx, cx, cy, gridRadius * (static_cast<float>(i) / 4.f), lineT, grid);
    ctx.rect(cx - lineT * 0.5f, cy - gridRadius, lineT, gridRadius * 2.f, grid);
    ctx.rect(cx - gridRadius, cy - lineT * 0.5f, gridRadius * 2.f, lineT, grid);
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 3.14159265f * 0.25f;
        const float px = std::cos(a) * gridRadius;
        const float py = std::sin(a) * gridRadius;
        const float pts[] = {cx - px, cy - py, cx + px, cy + py};
        ctx.polyline(pts, 2, lineT, withAlpha(grid, i % 2 == 0 ? 0.72f : 0.38f));
    }

    ctx.text("N", cx, y + 34.f * s, 28.f * s, k_textBright, HudAlign::Center, true);
    ctx.text("S", cx, y + ms - 48.f * s, 24.f * s, withAlpha(k_textBright, 0.62f), HudAlign::Center, true);
    ctx.text("W", x + 28.f * s, cy - 16.f * s, 24.f * s, withAlpha(k_textBright, 0.72f), HudAlign::Center, true);
    ctx.text("E", x + ms - 28.f * s, cy - 16.f * s, 24.f * s, withAlpha(k_textBright, 0.72f), HudAlign::Center, true);
    ctx.text("1", cx, y - 42.f * s, 34.f * s, k_textBright, HudAlign::Center, true);

    // Local player pointer.
    const float p = 30.f * s;
    ctx.triangle(cx, cy - p, cx - p * 0.58f, cy + p * 0.74f, cx + p * 0.58f, cy + p * 0.74f, k_primary);
    ctx.triangle(cx, cy - p * 0.45f, cx - p * 0.25f, cy + p * 0.35f, cx + p * 0.25f, cy + p * 0.35f, withAlpha(k_quaternary, 0.65f));

    // Enemy dots (red), rotated by yaw so player-forward is up. Dots beyond
    // the radar's range are clamped radially to the circular edge
    // so the player still gets a directional cue instead of a hard cull.
    const float worldToPixel = ms / (worldRange_ * 2.f);
    const float sinYaw = std::sin(localYaw_);
    const float cosYaw = std::cos(localYaw_);
    const float dotPx = dotSize * s;
    const float edgeMargin = (dotPx * 0.5f) + 1.f;
    const float maxDist = std::max(0.f, radius - edgeMargin);
    for (const auto& e : enemies_) {
        const float wdx = (e.worldX - localX_) * worldToPixel;
        const float wdz = (e.worldZ - localZ_) * worldToPixel;
        float dx = wdx * cosYaw - wdz * sinYaw;
        float dz = wdx * sinYaw + wdz * cosYaw;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > maxDist && dist > 1e-3f) {
            const float scale = maxDist / dist;
            dx *= scale;
            dz *= scale;
        }
        const float ex = cx - dx;
        const float ey = cy - dz;
        // Prototype-style yellow triangular blips.
        ctx.triangle(ex, ey - dotPx, ex - dotPx * 0.68f, ey + dotPx * 0.68f, ex + dotPx * 0.68f, ey + dotPx * 0.68f, k_yellow);
    }
}
