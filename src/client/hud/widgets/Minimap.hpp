/// @file Minimap.hpp
#pragma once

#include "hud/HudWidget.hpp"

#include <vector>

struct Minimap : HudWidget
{
    float mapSize = 180.f; ///< Pixel diameter of the minimap circle.
    float dotSize = 6.f;
    float borderThickness = 2.f;

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
