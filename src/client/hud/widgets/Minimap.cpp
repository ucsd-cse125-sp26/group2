/// @file Minimap.cpp
#include "Minimap.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>

Minimap::Minimap()
{
    anchor = HudAnchor::TopLeft;
    offsetX = 10.f;
    offsetY = 80.f; // Below TeamStatusBar area.
    width = 180.f;
    height = 180.f;
}

void Minimap::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    localX_ = state.localPlayerX;
    localZ_ = state.localPlayerZ;
    worldRange_ = state.minimapWorldRange;
    enemies_.clear();
    for (const auto& d : state.enemyDots)
        enemies_.push_back({d.worldX, d.worldZ});
}

void Minimap::draw(HudContext& ctx, float x, float y)
{
    const float s = uiScale_;
    const float ms = mapSize * s;
    const float ds = dotSize * s;
    const float bt = borderThickness * s;

    // Background.
    ctx.rect(x, y, ms, ms, HudColor(0.05f, 0.08f, 0.05f, 0.6f));
    ctx.rectOutline(x, y, ms, ms, bt, HudColor(0.3f, 0.5f, 0.3f, 0.8f));

    // Clip to minimap bounds.
    ctx.pushClipRect(x, y, ms, ms);

    const float cx = x + ms * 0.5f;
    const float cy = y + ms * 0.5f;

    // Local player dot (always center, green).
    ctx.rect(cx - ds * 0.5f, cy - ds * 0.5f, ds, ds, HudColor(0.f, 1.f, 0.f, 1.f));

    // Enemy dots (red) — world offset mapped to minimap pixels.
    const float worldToPixel = ms / (worldRange_ * 2.f);
    for (const auto& e : enemies_) {
        const float dx = (e.worldX - localX_) * worldToPixel;
        const float dz = (e.worldZ - localZ_) * worldToPixel;
        // World X → minimap X, World Z → minimap Y (top-down view).
        const float ex = cx + dx - ds * 0.5f;
        const float ey = cy + dz - ds * 0.5f;
        ctx.rect(ex, ey, ds, ds, HudColor(1.f, 0.2f, 0.2f, 0.9f));
    }

    ctx.popClipRect();
}
