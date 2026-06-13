/// @file HealthArmorBar.hpp
/// @brief Voidfall vitals bar with armor layered over health.

#pragma once

#include "hud/HudWidget.hpp"

struct HealthArmorBar : HudWidget
{
    float panelWidth = 630.f; ///< Base SVG frame width.
    float barHeight = 40.f;   ///< Base SVG frame height.
    float svgScale = 1.f;     ///< Extra scale applied to both SVG frame layers.
    float svgOffsetX = 0.f;   ///< SVG frame X adjustment.
    float svgOffsetY = 0.f;   ///< SVG frame Y adjustment.
    float svgStretchX = 1.f;  ///< Horizontal SVG stretch multiplier.
    float svgStretchY = 1.f;  ///< Vertical SVG stretch multiplier.

    HealthArmorBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int maxHealth_ = 100;
    int maxArmor_ = 100;
    int maxOverShield_ = 200;
    float healthFill_ = 1.f;
    float armorFill_ = 0.f;
    float overShieldFill_ = 0.f;
    float damagePowerupFill_ = 0.f;
};
