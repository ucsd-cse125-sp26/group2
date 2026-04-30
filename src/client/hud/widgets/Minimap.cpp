/// @file Minimap.cpp
#include "Minimap.hpp"

#include "hud/HudContext.hpp"

#include <SDL3/SDL.h>

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
    const float enemyDs = ds * 1.2f; // Enemy dots slightly bigger for visibility.
    const float bt = borderThickness * s;

    // Background.
    ctx.rect(x, y, ms, ms, HudColor(0.05f, 0.08f, 0.05f, 0.6f));
    ctx.rectOutline(x, y, ms, ms, bt, HudColor(0.3f, 0.5f, 0.3f, 0.8f));

    const float cx = x + ms * 0.5f;
    const float cy = y + ms * 0.5f;

    // Local player dot (always center, bright green with dark outline).
    const float pd = ds * 1.5f; // Player dot is a bit larger.
    ctx.rect(cx - pd * 0.5f - 1.f, cy - pd * 0.5f - 1.f, pd + 2.f, pd + 2.f, HudColor(0.f, 0.f, 0.f, 0.8f));
    ctx.rect(cx - pd * 0.5f, cy - pd * 0.5f, pd, pd, HudColor(0.f, 1.f, 0.f, 1.f));

    // Enemy dots (red) — world offset mapped to minimap pixels.
    const float worldToPixel = ms / (worldRange_ * 2.f);
    for (const auto& e : enemies_) {
        const float dx = (e.worldX - localX_) * worldToPixel;
        const float dz = (e.worldZ - localZ_) * worldToPixel;
        // World X → minimap X, World Z → minimap Y (top-down view).
        const float ex = cx + dx;
        const float ey = cy + dz;
        // Only draw if within minimap bounds (with some margin).
        if (ex > x && ex < x + ms && ey > y && ey < y + ms) {
            ctx.rect(ex - enemyDs * 0.5f, ey - enemyDs * 0.5f, enemyDs, enemyDs, HudColor(1.f, 0.15f, 0.15f, 1.f));
        }
    }
}
