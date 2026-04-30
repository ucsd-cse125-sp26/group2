/// @file TeamStatusBar.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct TeamStatusBar : HudWidget
{
    float indicatorSize = 12.f;
    float indicatorSpacing = 4.f;
    float scoreFontSize = 20.f;

    TeamStatusBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int allyAlive_ = 0, allyTotal_ = 0;
    int enemyAlive_ = 0, enemyTotal_ = 0;
    int allyScore_ = 0, enemyScore_ = 0;
};
