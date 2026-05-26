/// @file GrenadeSlotsWidget.hpp
/// @brief Three-slot grenade inventory row.

#pragma once

#include "hud/HudWidget.hpp"

struct GrenadeSlotsWidget : HudWidget
{
    float trayWidth = 500.f;
    float trayHeight = 150.f;
    float slotSize = 128.f;
    float slotGap = 18.f;
    float iconSize = 76.f;
    float countFontSize = 34.f;
    float countPadX = 88.f;
    float countPadY = 28.f;
    float countCharacterGap = 3.25f;
    float iconPadRight = 3.f;
    float cornerCut = 2.25f;
    float borderThickness = 4.f;

    GrenadeSlotsWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudGrenadeRadialState state_;
};
