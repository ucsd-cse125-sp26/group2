/// @file HealthArmorBar.hpp
/// @brief Voidfall vitals bar with armor layered over health.

#pragma once

#include "hud/HudWidget.hpp"

struct HealthArmorBar : HudWidget
{
    float panelWidth = 650.f;     ///< Total bar width in the 2848x1494 prototype canvas.
    float barHeight = 42.f;       ///< Total bar height.
    float chamferSize = 38.f;     ///< Left and right slant depth.
    float cornerCutSize = 10.f;   ///< Small lower-corner cuts that soften the side points.
    float outlineThickness = 3.f; ///< Hairline thickness around the full silhouette.

    HealthArmorBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int maxHealth_ = 100;
    int maxArmor_ = 100;
    float healthFill_ = 1.f;
    float armorFill_ = 0.f;
};
