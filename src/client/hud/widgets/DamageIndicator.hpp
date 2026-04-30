/// @file DamageIndicator.hpp
#pragma once

#include "hud/HudWidget.hpp"

#include <vector>

struct DamageIndicator : HudWidget
{
    float arcDistance = 60.f; ///< Distance from center.
    float arcLength = 24.f;   ///< Arc segment length.
    float arcThickness = 4.f;
    float fadeTime = 0.8f;

    DamageIndicator();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Arc
    {
        float angleDeg = 0.f;
        float timer = 0.f;
    };
    std::vector<Arc> arcs_;
};
