/// @file BuyMenu.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct BuyMenu : HudWidget
{
    float panelWidth = 450.f;
    float panelHeight = 380.f;
    float fontSize = 20.f;
    float itemHeight = 34.f;

    BuyMenu();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;
    void toggle(bool isBuyPhase);

private:
    bool isBuyPhase_ = false;
    float openAlpha_ = 0.f;
};
