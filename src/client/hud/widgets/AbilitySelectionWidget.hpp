/// @file AbilitySelectionWidget.hpp
/// @brief Center-screen level-up ability choice prompt.

#pragma once

#include "hud/HudWidget.hpp"

#include <array>
#include <string>

struct AbilitySelectionWidget : HudWidget
{
    float panelWidth = 520.f;
    float choiceWidth = 244.f;
    float choiceHeight = 90.f;
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
    // Per visual slot (0 = left card, 1 = right card): which option index it
    // renders and the key glyph that selects it. On controller the cards swap
    // so the physically-left trigger picks the left card. Recomputed in update().
    std::array<int, 2> slotOption_{0, 1};
    std::array<std::string, 2> slotTag_{"LMB", "RMB"};
};
