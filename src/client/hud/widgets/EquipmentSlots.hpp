/// @file EquipmentSlots.hpp
/// @brief Bottom-center ability cluster with SVG placeholder icons and charge bars.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>

struct EquipmentSlots : HudWidget
{
    float slotSize = 118.f;
    float slotHeight = 92.f;
    float slotGap = 24.f;
    float iconSize = 56.f;
    float chargeBarWidth = 360.f;
    float chargeBarHeight = 24.f;
    float keyFontSize = 26.f;
    float keyPadX = 10.f;
    float keyPadY = 3.5f;

    EquipmentSlots();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudEquipmentState state_;
    std::string primaryAbilityLabel_ = "Left Shift";
    std::string secondaryAbilityLabel_ = "E";
};
