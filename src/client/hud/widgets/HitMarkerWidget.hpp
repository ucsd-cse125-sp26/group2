/// @file HitMarkerWidget.hpp
/// @brief Center-screen hit confirm flare with fade + scale animation.

#pragma once

#include "hud/HudWidget.hpp"

struct HitMarkerWidget : HudWidget
{
    float armLength = 12.f;
    float armThickness = 2.5f;
    float armGap = 5.f;
    float fadeDuration = 0.35f;

    HitMarkerWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float alpha_ = 0.f;
    float scale_ = 1.f;
    bool isHeadshot_ = false;
};
