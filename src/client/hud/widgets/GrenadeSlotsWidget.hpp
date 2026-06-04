/// @file GrenadeSlotsWidget.hpp
/// @brief Three-slot grenade inventory row.

#pragma once

#include "hud/HudWidget.hpp"

struct GrenadeSlotsWidget : HudWidget
{
    float slotSize = 70.f;
    float slotGap = 23.5f;
    float iconSize = 40.f;
    float countFontSize = 20.f;
    float countPadX = 9.f;
    float countPadY = 5.f;
    float countCharacterGap = 3.25f;
    float iconPadRight = 3.f;
    float elementsRotationDeg = 11.5f;
    float backgroundWidth = 226.f;
    float backgroundHeight = 70.f;
    float backgroundScale = 2.25f;
    float backgroundOffsetX = -135.f;
    float backgroundOffsetY = -44.5f;
    float backgroundStretchX = 0.55f;
    float backgroundStretchY = 1.f;
    float backgroundRotationDeg = -6.f;

    GrenadeSlotsWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudGrenadeRadialState state_;
};
