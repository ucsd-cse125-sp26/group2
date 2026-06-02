/// @file Minimap.cpp
/// @brief Voidfall radar SVG with yaw-relative enemy dots.

#include "Minimap.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <cmath>

namespace
{

void drawCircleOutline(HudContext& ctx, float cx, float cy, float radius, float thickness, HudColor color)
{
    constexpr int kSegments = 96;
    float points[(kSegments + 1) * 2]{};
    for (int i = 0; i <= kSegments; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(kSegments)) * 2.f * 3.14159265f;
        points[i * 2 + 0] = cx + std::cos(a) * radius;
        points[i * 2 + 1] = cy + std::sin(a) * radius;
    }
    ctx.polyline(points, kSegments + 1, thickness, color);
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
    dotSize = 12.f;
    dotZoneRadius = 76.f;
    dotZoneOffsetX = -0.5f;
    dotZoneOffsetY = 3.f;
}

void Minimap::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    localX_ = state.localPlayerX;
    localZ_ = state.localPlayerZ;
    localYaw_ = state.localPlayerYaw;
    worldRange_ = state.minimapWorldRange;

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
    const float zoneCx = cx + dotZoneOffsetX * s;
    const float zoneCy = cy + dotZoneOffsetY * s;
    const float zoneRadius = std::max(0.f, dotZoneRadius * s);

    ctx.svg(HudIcon::Radar, x, y, ms, ms);

    if (showDotZoneDebug && zoneRadius > 0.f)
        drawCircleOutline(ctx, zoneCx, zoneCy, zoneRadius, std::max(1.f, 1.5f * s), withAlpha(k_amber, 0.9f));

    // Enemy dots (red), rotated by yaw so player-forward is up. Dots beyond
    // the radar's range are clamped radially to the configured circular zone
    // so the player still gets a directional cue instead of a hard cull.
    const float safeRange = std::max(1.f, worldRange_);
    const float worldToPixel = ms / (safeRange * 2.f);
    const float sinYaw = std::sin(localYaw_);
    const float cosYaw = std::cos(localYaw_);
    const float dotPx = dotSize * s;
    const float edgeMargin = (dotPx * 0.5f) + 1.f;
    const float maxDist = std::max(0.f, zoneRadius - edgeMargin);
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
        const float ex = zoneCx - dx;
        const float ey = zoneCy - dz;
        ctx.roundedRect(ex - dotPx * 0.5f, ey - dotPx * 0.5f, dotPx, dotPx, dotPx * 0.5f, k_red);
    }
}
