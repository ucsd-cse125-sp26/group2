/// @file KdaCounter.hpp
/// @brief Top-right K/A/D counter — large amber "K", medium "A" + "D".
///
/// Three-column inline panel:
///   ┌─────┬──┬─────┬──┬─────┐
///   │ 14  │  │  4  │  │  9  │
///   │  K  │  │  A  │  │  D  │
///   └─────┴──┴─────┴──┴─────┘
/// "K" is the hero numeral (largest, amber); "A" + "D" are smaller and dim.

#pragma once

#include "hud/HudWidget.hpp"

struct KdaCounter : HudWidget
{
    float kFontSize = 22.f;
    float adFontSize = 16.f;
    float labelFontSize = 8.f;
    float panelPadX = 12.f;
    float panelPadY = 6.f;
    float colGap = 14.f;

    KdaCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int kills_ = 0;
    int assists_ = 0;
    int deaths_ = 0;
};
