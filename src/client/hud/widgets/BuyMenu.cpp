/// @file BuyMenu.cpp
#include "BuyMenu.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"
#include "hud/VoidfallStyle.hpp"

#include <cmath>

using namespace voidfall;

BuyMenu::BuyMenu()
{
    anchor = HudAnchor::Center;
    visible = false;
}

void BuyMenu::toggle(bool isBuyPhase)
{
    isBuyPhase_ = isBuyPhase;
    if (!isBuyPhase)
        visible = false;
    else
        visible = !visible;
}

void BuyMenu::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    isBuyPhase_ = state.isBuyPhase;
    if (!isBuyPhase_)
        visible = false;

    const float targetAlpha = visible ? 1.f : 0.f;
    if (std::abs(openAlpha_ - targetAlpha) > 0.01f)
        tweens.tween(&openAlpha_, targetAlpha, 0.15f, easeOutQuad);
}

void BuyMenu::draw(HudContext& ctx, float cx, float cy)
{
    const float alpha = debugForceOpen_ ? 1.f : openAlpha_;
    if (alpha < 0.01f)
        return;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float ph = panelHeight * s;
    const float fs = fontSize * s;
    const float ih = itemHeight * s;

    const float x = cx - pw * 0.5f;
    const float y = cy - ph * 0.5f;

    ctx.rect(x, y, pw, ph, withAlpha(k_quaternary, 0.9f * alpha));
    ctx.rectOutline(x, y, pw, ph, 1.f * s, withAlpha(k_secondary, alpha));

    ctx.text("BUY MENU", cx, y + 10.f * s, 24.f * s, withAlpha(k_primary, alpha), HudAlign::Center);

    // Placeholder weapon list.
    const char* weapons[] = {"1. Rifle", "2. Shotgun", "3. Railgun", "4. Rocket Launcher"};
    float itemY = y + 50.f * s;
    for (const char* w : weapons) {
        ctx.text(w, x + 20.f * s, itemY, fs, withAlpha(k_tertiary, alpha));
        itemY += ih;
    }

    ctx.text("[B] Close", cx, y + ph - 30.f * s, 14.f * s, withAlpha(k_secondary, alpha), HudAlign::Center);
}
