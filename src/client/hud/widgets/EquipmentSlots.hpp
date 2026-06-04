/// @file EquipmentSlots.hpp
/// @brief Bottom-center two-ability SVG cooldown widget.

#pragma once

#include "hud/HudWidget.hpp"

#include <array>
#include <string>

struct EquipmentSlots : HudWidget
{
    struct SvgComponentTuning
    {
        float scale = 1.f;
        float offsetX = 0.f;
        float offsetY = 0.f;
        float stretchX = 1.f;
        float stretchY = 1.f;
    };

    struct AbilityElementTuning
    {
        SvgComponentTuning iconFrame;
        SvgComponentTuning icon;
        SvgComponentTuning bar;
        float bindingOffsetX = 0.f;
        float bindingOffsetY = 0.f;
        bool flipIconX = false;
        bool flipIconY = false;
        bool flipBarX = false;
        bool flipBarY = false;
    };

    float iconFrameWidth = 92.f;
    float iconFrameHeight = 87.f;
    float abilityIconSize = 54.f;
    float bindingFontSize = 20.f;
    float barWidth = 220.f;
    float barHeight = 87.f;
    float iconBarGap = 0.f;
    float centerGap = 0.f;

    std::array<AbilityElementTuning, 2> abilityElements{};

    EquipmentSlots();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudEquipmentState state_;
    std::array<std::string, 2> bindingLabels_{"", ""};
};
