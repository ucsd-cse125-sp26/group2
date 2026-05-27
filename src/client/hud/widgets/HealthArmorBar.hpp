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
    float visorWidth = 1020.f;    ///< Total decorative visor wing span.
    float visorOffsetX = 0.f;     ///< Horizontal offset from the health bar center.
    float visorTopY = 1.5f;       ///< Top visor endpoint Y in prototype-space pixels.
    float visorFrameRatio = 0.46f; ///< Vertical attachment point as a fraction of bar height.
    float visorFrameOffsetY = 0.f; ///< Extra vertical offset for the visor attachment point.
    float visorWingInset = 230.f; ///< Distance from the outer endpoint to the bend.
    float visorInnerGap = 58.f;   ///< Gap between the bar edge and inner wing endpoint.
    float visorThickness = 2.5f;  ///< Main visor decal stroke thickness.
    float visorCoreThickness = 1.f; ///< Bright inner visor stroke thickness.

    HealthArmorBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int maxHealth_ = 100;
    int maxArmor_ = 100;
    float healthFill_ = 1.f;
    float armorFill_ = 0.f;
};
