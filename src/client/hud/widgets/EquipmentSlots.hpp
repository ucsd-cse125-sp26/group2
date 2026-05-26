/// @file EquipmentSlots.hpp
/// @brief Bottom-center ability cluster with SVG placeholder icons and charge bars.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>

struct EquipmentSlots : HudWidget
{
    float clusterWidth = 1040.f;
    float clusterHeight = 180.f;
    float slotSize = 170.f;
    float slotHeight = 122.f;
    float slotGap = 50.f;
    float iconSize = 82.f;
    float chargeBarWidth = 450.f;
    float chargeBarHeight = 32.f;
    float keyFontSize = 34.f;
    float keyPadX = 13.f;
    float keyPadY = 7.f;

    EquipmentSlots();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudEquipmentState state_;
    std::string primaryAbilityLabel_ = "Left Shift";
    std::string secondaryAbilityLabel_ = "E";
};
