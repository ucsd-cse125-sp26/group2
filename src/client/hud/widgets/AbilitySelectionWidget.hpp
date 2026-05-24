/// @file AbilitySelectionWidget.hpp
/// @brief Center-screen level-up ability choice prompt.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>

struct AbilitySelectionWidget : HudWidget
{
    float panelWidth = 520.f;
    float choiceWidth = 244.f;
    float choiceHeight = 86.f;
    float choiceGap = 12.f;
    float headerFontSize = 20.f;
    float nameFontSize = 20.f;
    float bodyFontSize = 18.f;
    float keyFontSize = 18.f;

    AbilitySelectionWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudAbilitySelectionState state_;
    std::string abilityMenuLabel_ = "Left Alt";
};
