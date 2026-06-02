/// @file Minimap.hpp
#pragma once

#include "hud/HudWidget.hpp"

#include <vector>

struct Minimap : HudWidget
{
    float mapSize = 180.f; ///< Pixel size of the Radar.svg quad.
    float dotSize = 12.f;
    float dotZoneRadius = 76.f;     ///< Radius of the enemy-dot projection circle, in unscaled HUD pixels.
    float dotZoneOffsetX = -0.5f;   ///< X offset of the dot projection circle from the SVG center.
    float dotZoneOffsetY = 3.f;     ///< Y offset of the dot projection circle from the SVG center.
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
