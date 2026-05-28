/// @file RailgunScopeWidget.hpp
/// @brief Railgun scope overlay shown while holding right click.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Full-screen scope mask, reticle, and railgun charge readout.
struct RailgunScopeWidget : HudWidget
{
    RailgunScopeWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float screenW_ = 1280.f;
    float screenH_ = 720.f;
    float chargeTime_ = 0.f;
};
