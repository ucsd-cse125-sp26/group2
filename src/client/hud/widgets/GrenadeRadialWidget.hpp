/// @file GrenadeRadialWidget.hpp
/// @brief Held-G grenade selection radial.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>

struct GrenadeRadialWidget : HudWidget
{
    float radius = 126.0f;
    float cardWidth = 118.0f;
    float cardHeight = 48.0f;
    float nameFontSize = 13.0f;
    float countFontSize = 18.0f;
    float keyFontSize = 11.0f;

    GrenadeRadialWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudGrenadeRadialState state_;
    std::string keyLabel_ = "G";
};
