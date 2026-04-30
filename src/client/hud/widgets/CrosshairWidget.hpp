/// @file CrosshairWidget.hpp
/// @brief Dynamic crosshair with configurable gap, thickness, and dot.

#pragma once

#include "hud/HudWidget.hpp"

struct CrosshairWidget : HudWidget
{
    CrosshairStyle style;

    CrosshairWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;
};
