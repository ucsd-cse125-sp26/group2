/// @file BuyMenu.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct BuyMenu : HudWidget
{
    float panelWidth = 400.f;
    float panelHeight = 350.f;
    float fontSize = 16.f;
    float itemHeight = 30.f;

    BuyMenu();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;
    void toggle(bool isBuyPhase);

private:
    bool isBuyPhase_ = false;
    float openAlpha_ = 0.f;
};
