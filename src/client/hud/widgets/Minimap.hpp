/// @file Minimap.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct Minimap : HudWidget
{
    float mapSize = 150.f; ///< Pixel width/height of the minimap square.
    float dotSize = 4.f;
    float borderThickness = 2.f;

    Minimap();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;
};
