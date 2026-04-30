/// @file RoundTimer.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct RoundTimer : HudWidget
{
    float fontSize = 24.f;
    float lowTimeThreshold = 10.f; ///< Seconds — color shifts to red below this.

    RoundTimer();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float timeRemaining_ = 0.f;
};
