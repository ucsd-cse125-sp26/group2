/// @file HealthArmorBar.hpp
/// @brief Health and armor bars with animated fill.

#pragma once

#include "hud/HudWidget.hpp"

struct HealthArmorBar : HudWidget
{
    // Layout constants (tweakable via ImGui debug panel).
    float barWidth = 200.f;
    float barHeight = 16.f;
    float barSpacing = 4.f;
    float fontSize = 18.f;
    float textPadding = 6.f;

    HealthArmorBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float healthFill_ = 1.f;
    float armorFill_ = 0.f;
    int lastHealth_ = 100;
    int lastArmor_ = 0;
    int displayHealth_ = 100;
    int displayArmor_ = 0;
};
