/// @file GravityIndicator.hpp
/// @brief Bottom-right gravity-direction widget — small circle with arrow.
///
/// Shows where "down" is, since VOIDFALL flips gravity at runtime.
/// Direction values: 0=down, 1=left, 2=up, 3=right.

#pragma once

#include "hud/HudWidget.hpp"

struct GravityIndicator : HudWidget
{
    float diskSize = 56.f;

    GravityIndicator();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int direction_ = 0; // 0=down, 1=left, 2=up, 3=right
};
