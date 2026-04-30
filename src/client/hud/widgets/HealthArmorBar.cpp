/// @file HealthArmorBar.cpp
#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

#include <SDL3/SDL.h>

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::BottomLeft;
    offsetX = 20.f;
    offsetY = -60.f;
    width = 200.f;
    height = 40.f;
}

void HealthArmorBar::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    visible = state.isAlive;
    displayHealth_ = state.health;
    displayArmor_ = state.armor;

    const float targetHealth = (state.maxHealth > 0) ? static_cast<float>(state.health) / state.maxHealth : 0.f;
    const float targetArmor = (state.maxArmor > 0) ? static_cast<float>(state.armor) / state.maxArmor : 0.f;

    if (state.health != lastHealth_) {
        tweens.tween(&healthFill_, targetHealth, 0.25f, easeOutQuad);
        lastHealth_ = state.health;
    }
    if (state.armor != lastArmor_) {
        tweens.tween(&armorFill_, targetArmor, 0.25f, easeOutQuad);
        lastArmor_ = state.armor;
    }
}

void HealthArmorBar::draw(HudContext& ctx, float x, float y)
{
    const float s = uiScale_;
    const float bw = barWidth * s;
    const float bh = barHeight * s;
    const float bs = barSpacing * s;
    const float fs = fontSize * s;
    const float tp = textPadding * s;

    // Health bar.
    ctx.bar(x, y, bw, bh, healthFill_, HudColor(0.2f, 0.8f, 0.2f, 0.9f), HudColor(0.15f, 0.15f, 0.15f, 0.7f));

    // Health text.
    char hpText[16];
    SDL_snprintf(hpText, sizeof(hpText), "%d", displayHealth_);
    ctx.text(hpText, x + bw + tp, y, fs, HudColor::white());

    // Armor bar (below health).
    const float armorY = y + bh + bs;
    ctx.bar(x, armorY, bw, bh, armorFill_, HudColor(0.3f, 0.5f, 0.9f, 0.9f), HudColor(0.15f, 0.15f, 0.15f, 0.7f));

    // Armor text.
    char armorText[16];
    SDL_snprintf(armorText, sizeof(armorText), "%d", displayArmor_);
    ctx.text(armorText, x + bw + tp, armorY, fs, HudColor::white());
}
