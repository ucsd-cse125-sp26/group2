/// @file DamageAccumWidget.hpp
/// @brief Running damage total displayed below the crosshair.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Shows accumulated damage dealt to the current target.
///
/// Positioned just below the crosshair.  Hidden when total is 0.
/// Resets after a short delay or when switching targets (handled by Game).
struct DamageAccumWidget : HudWidget
{
    DamageAccumWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayTotal_ = 0;
    float alpha_ = 0.f;
    HudColor color_{1.f, 1.f, 1.f, 1.f}; ///< Matches latest hit type.
};
