/// @file LevelBarWidget.hpp
/// @brief Ability level progress bar below the ability slots.

#pragma once

#include "hud/HudWidget.hpp"

struct LevelBarWidget : HudWidget
{
    float barWidth = 790.5f;
    float barHeight = 39.5f;
    float svgScale = 0.95f;
    float svgOffsetX = 0.f;
    float svgOffsetY = 0.f;
    float svgStretchX = 0.57f;
    float svgStretchY = 1.f;
    float svgRotationDeg = 0.f;

    LevelBarWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float progress_ = 0.f;
};
