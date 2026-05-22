/// @file Minimap.cpp
/// @brief Voidfall circular radar with clipped grid + amber player chevron.

#include "Minimap.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
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

void drawClockwiseRingArc(HudContext& ctx, float cx, float cy, float radius, float progress, float thickness, HudColor color)
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
    offsetX = 80.f;
    offsetY = -260.f;
    width = 200.f;
    height = 200.f;
    mapSize = 200.f;
    dotSize = 6.f;
    borderThickness = 1.f;
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
        drawCircleOutline(ctx, cx, cy, ringRadius, ringT, withAlpha(k_lineDim, 0.55f));
        if (trailLevel_ > liveLevel_)
            drawClockwiseRingArc(ctx, cx, cy, ringRadius, trailLevel_, ringT, withAlpha(k_amberDim, 0.65f));
        drawClockwiseRingArc(ctx, cx, cy, ringRadius, liveLevel_, ringT, k_amber);
    }

    ctx.roundedRect(x, y, ms, ms, radius, HudColor{0.10f, 0.09f, 0.08f, 0.85f});
    const float borderT = std::max(0.f, borderThickness * s);
    if (borderT > 0.f)
        drawCircleOutline(ctx, cx, cy, std::max(0.f, radius - borderT * 0.5f), borderT, k_line);

    // Grid clipped to circular chords.
    const HudColor grid = HudColor{0.27f, 0.26f, 0.25f, 0.45f};
    const int divisions = 10;
    const float lineT = std::max(1.f, 1.f * s);
    const float gridRadius = std::max(0.f, radius - borderT - lineT * 0.5f);
    for (int i = 1; i < divisions; ++i) {
        const float offset = -radius + (static_cast<float>(i) / divisions) * ms;
        const float halfChord = std::sqrt(std::max(0.f, gridRadius * gridRadius - offset * offset));
        if (halfChord <= 0.f)
            continue;
        ctx.rect(cx + offset - lineT * 0.5f, cy - halfChord, lineT, halfChord * 2.f, grid);
        ctx.rect(cx - halfChord, cy + offset - lineT * 0.5f, halfChord * 2.f, lineT, grid);
    }

    // Local player chevron — shared notched-arrow glyph from the icon module.
    icons::playerArrow(ctx, std::round(cx), std::round(cy), 14.f * s, k_amber);

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
        // Square dot for the mil-spec feel (rotated 45° = diamond).
        ctx.rotatedRect(ex, ey, dotPx, dotPx, 45.f, k_red);
    }
}
