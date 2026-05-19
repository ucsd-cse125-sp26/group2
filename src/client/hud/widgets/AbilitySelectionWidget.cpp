/// @file AbilitySelectionWidget.cpp
/// @brief Center-screen level-up ability choice prompt.

#include "AbilitySelectionWidget.hpp"

#include "config/InputBindings.hpp"
#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>

AbilitySelectionWidget::AbilitySelectionWidget()
{
    anchor = HudAnchor::Center;
    offsetX = 0.0f;
    offsetY = 120.0f;
}

void AbilitySelectionWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    state_ = state.abilitySelection;
    if (state.bindings) {
        abilityMenuLabel_ = InputBindings::bindingLabel(state.bindings->get(Action::AbilityMenu));
    }
    visible = state.isAlive && state_.available;
}

namespace
{

void drawKeyTag(HudContext& ctx, const char* label, float x, float y, float fs, bool active)
{
    using namespace voidfall;
    const float padX = 7.0f;
    const float padY = 3.0f;
    const float w = ctx.measureText(label, fs) + padX * 2.0f;
    const float h = fs + padY * 2.0f;
    const HudColor border = active ? k_amber : k_lineBright;
    const HudColor fill = active ? HudColor{0.32f, 0.22f, 0.07f, 0.88f} : HudColor{0.0f, 0.0f, 0.0f, 0.55f};
    ctx.rect(x, y, w, h, fill);
    ctx.rectOutline(x, y, w, h, 1.0f, border);
    ctx.text(label, x + w * 0.5f, y + padY - fs * 0.18f, fs, active ? k_amber : k_textDim, HudAlign::Center);
}

} // namespace

void AbilitySelectionWidget::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float headerFs = headerFontSize * s;
    const float keyFs = keyFontSize * s;

    char header[96];
    std::snprintf(header,
                  sizeof(header),
                  "LEVEL %d %s AVAILABLE",
                  std::max(1, state_.level),
                  state_.slotLabel.empty() ? "ABILITY" : state_.slotLabel.c_str());

    if (!state_.modifierHeld) {
        const float panelW = 270.0f * s;
        const float panelH = 58.0f * s;
        const float x = anchorX - panelW * 0.5f;
        const float y = anchorY - panelH * 0.5f;

        drawPanel(ctx, x, y, panelW, panelH, HudColor{0.06f, 0.07f, 0.08f, 0.78f}, k_lineBright, 1.0f);
        drawCornerBrackets(ctx, x, y, panelW, panelH, 8.0f * s, 1.0f, 2.0f * s, k_lineBright);
        ctx.text(header, anchorX, y + 10.0f * s, headerFs, k_amber, HudAlign::Center, true);
        char prompt[128];
        std::snprintf(prompt, sizeof(prompt), "HOLD %s TO CHOOSE", abilityMenuLabel_.c_str());
        ctx.text(prompt, anchorX, y + 34.0f * s, keyFs, k_textDim, HudAlign::Center, true);
        return;
    }

    const float choiceW = choiceWidth * s;
    const float choiceH = choiceHeight * s;
    const float gap = choiceGap * s;
    const float totalW = choiceW * 2.0f + gap;
    const float x0 = anchorX - totalW * 0.5f;
    const float y0 = anchorY - choiceH * 0.5f;
    const float nameFs = nameFontSize * s;
    const float bodyFs = bodyFontSize * s;

    ctx.text(header, anchorX, y0 - 42.0f * s, headerFs, k_amber, HudAlign::Center, true);
    char holdPrompt[96];
    std::snprintf(holdPrompt, sizeof(holdPrompt), "HOLD %s", abilityMenuLabel_.c_str());
    ctx.text(holdPrompt, anchorX, y0 - 24.0f * s, keyFs, k_amber, HudAlign::Center, true);

    for (int i = 0; i < 2; ++i) {
        const auto& choice = state_.choices[static_cast<std::size_t>(i)];
        const float x = x0 + static_cast<float>(i) * (choiceW + gap);
        const bool active = state_.modifierHeld;
        const HudColor border = active ? k_amber : k_lineBright;
        const HudColor fill = i == 0 ? HudColor{0.09f, 0.10f, 0.11f, 0.92f} : HudColor{0.11f, 0.09f, 0.10f, 0.92f};

        drawPanel(ctx, x, y0, choiceW, choiceH, fill, border, 1.0f);
        drawCornerBrackets(ctx, x, y0, choiceW, choiceH, 10.0f * s, 1.0f, 2.0f * s, active ? k_amber : k_lineBright);

        drawKeyTag(ctx, i == 0 ? "LMB" : "RMB", x + 10.0f * s, y0 + 9.0f * s, keyFs, active);
        ctx.text(choice.name.c_str(), x + 14.0f * s, y0 + 35.0f * s, nameFs, k_textBright, HudAlign::Left, true);
        ctx.text(choice.description.c_str(), x + 14.0f * s, y0 + 62.0f * s, bodyFs, k_textDim, HudAlign::Left);
    }
}
