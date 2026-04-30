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

    // Local player dot (always center, circular, bright green).
    const float pd = ds;
    ctx.roundedRect(cx - pd * 0.5f, cy - pd * 0.5f, pd, pd, pd * 0.5f, HudColor(0.f, 1.f, 0.f, 1.f));

    // Enemy dots (red circles) — world offset mapped to minimap pixels.
    const float worldToPixel = ms / (worldRange_ * 2.f);
    for (const auto& e : enemies_) {
        const float dx = (e.worldX - localX_) * worldToPixel;
        const float dz = (e.worldZ - localZ_) * worldToPixel;
        const float ex = cx + dx;
        const float ey = cy + dz;
        if (ex > x && ex < x + ms && ey > y && ey < y + ms) {
            ctx.roundedRect(ex - enemyDs * 0.5f,
                            ey - enemyDs * 0.5f,
                            enemyDs,
                            enemyDs,
                            enemyDs * 0.5f,
                            HudColor(1.f, 0.15f, 0.15f, 1.f));
        }
    }
}
