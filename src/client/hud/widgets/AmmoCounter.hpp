/// @file AmmoCounter.hpp
/// @brief Ammo clip / reserve display.

#pragma once

#include "hud/HudWidget.hpp"

struct AmmoCounter : HudWidget
{
    float clipFontSize = 42.f;
    float reserveFontSize = 22.f;
    float dividerPadding = 6.f;

    AmmoCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayClip_ = 30;
    int displayReserve_ = 90;
};
