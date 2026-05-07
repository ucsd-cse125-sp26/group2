/// @file Minimap.cpp
/// @brief Voidfall square radar — grid + mil-spec bracket corners + amber player chevron.

#include "Minimap.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <cmath>

Minimap::Minimap()
{
    anchor = HudAnchor::TopLeft;
    offsetX = 24.f;
    offsetY = 24.f;
    width = 200.f;
    height = 200.f;
    mapSize = 200.f;
    dotSize = 6.f;
    borderThickness = 1.f;
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

    // Frame.
    drawPanel(ctx, x, y, ms, ms, HudColor{0.10f, 0.09f, 0.08f, 0.85f}, k_line, 1.f);

    // Grid (10×10 cells of light hairlines, like graph paper in the prototype).
    const HudColor grid = HudColor{0.27f, 0.26f, 0.25f, 0.45f};
    const int divisions = 10;
    for (int i = 1; i < divisions; ++i) {
        const float t = (static_cast<float>(i) / divisions) * ms;
        ctx.rect(x + t, y + 1.f, 1.f, ms - 2.f, grid);
        ctx.rect(x + 1.f, y + t, ms - 2.f, 1.f, grid);
    }

    // Mil-spec bracket corners (sit slightly outside the frame).
    drawCornerBrackets(ctx, x, y, ms, ms, 14.f * s, 1.f * s, 2.f * s, k_amber);

    const float cx = x + ms * 0.5f;
    const float cy = y + ms * 0.5f;

    // Local player chevron — amber upward-pointing arrow at center.
    {
        const float arrow = 8.f * s;
        // Body line.
        ctx.rect(cx - 0.75f * s, cy - arrow * 0.5f, 1.5f * s, arrow, k_amber);
        // Head: two short legs forming an arrow tip.
        ctx.rotatedRect(cx - arrow * 0.45f, cy - arrow * 0.30f, 1.5f * s, arrow * 0.7f, -45.f, k_amber);
        ctx.rotatedRect(cx + arrow * 0.45f, cy - arrow * 0.30f, 1.5f * s, arrow * 0.7f, 45.f, k_amber);
    }

    // Enemy dots (red), rotated by yaw so player-forward is up.
    const float worldToPixel = ms / (worldRange_ * 2.f);
    const float sinYaw = std::sin(localYaw_);
    const float cosYaw = std::cos(localYaw_);
    const float dotPx = dotSize * s;
    for (const auto& e : enemies_) {
        const float wdx = (e.worldX - localX_) * worldToPixel;
        const float wdz = (e.worldZ - localZ_) * worldToPixel;
        const float dx = wdx * cosYaw - wdz * sinYaw;
        const float dz = wdx * sinYaw + wdz * cosYaw;
        const float ex = cx - dx;
        const float ey = cy - dz;
        if (ex > x + 1.f && ex < x + ms - 1.f && ey > y + 1.f && ey < y + ms - 1.f) {
            // Square dot for the mil-spec feel (rotated 45° = diamond).
            ctx.rotatedRect(ex, ey, dotPx, dotPx, 45.f, k_red);
        }
    }

    (void)borderThickness; // border thickness handled by drawPanel.
}
