/// @file Minimap.hpp
#pragma once

#include "hud/HudWidget.hpp"

#include <vector>

struct Minimap : HudWidget
{
    float mapSize = 275.f; ///< Pixel size of the Radar.svg quad.
    float dotSize = 12.25f;
    float dotZoneRadius = 109.5f; ///< Radius of the enemy-dot projection circle, in unscaled HUD pixels.
    float dotZoneOffsetX = -1.f;  ///< X offset of the dot projection circle from the SVG center.
    float dotZoneOffsetY = 4.f;   ///< Y offset of the dot projection circle from the SVG center.
    bool showDotZoneDebug = false;

    Minimap();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float localX_ = 0.f, localZ_ = 0.f;
    float localYaw_ = 0.f; ///< Player yaw in radians.
    float worldRange_ = 100.f;
    struct Dot
    {
        float worldX, worldZ;
    };
    std::vector<Dot> enemies_;
};
