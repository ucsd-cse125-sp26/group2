/// @file BuyMenu.cpp
#include "BuyMenu.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

#include <cmath>

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
    if (openAlpha_ < 0.01f)
        return;

    const float x = cx - panelWidth * 0.5f;
    const float y = cy - panelHeight * 0.5f;

    ctx.rect(x, y, panelWidth, panelHeight, HudColor(0.06f, 0.06f, 0.12f, 0.9f * openAlpha_));
    ctx.rectOutline(x, y, panelWidth, panelHeight, 1.f, HudColor(0.5f, 0.4f, 0.2f, openAlpha_));

    ctx.text("BUY MENU", cx, y + 10.f, 20.f, HudColor(1.f, 0.85f, 0.3f, openAlpha_), HudAlign::Center);

    // Placeholder weapon list.
    const char* weapons[] = {"1. Rifle", "2. Shotgun", "3. Railgun", "4. Rocket Launcher"};
    float itemY = y + 50.f;
    for (const char* w : weapons) {
        ctx.text(w, x + 20.f, itemY, fontSize, HudColor(1.f, 1.f, 1.f, openAlpha_));
        itemY += itemHeight;
    }

    ctx.text("[B] Close", cx, y + panelHeight - 30.f, 12.f, HudColor(0.6f, 0.6f, 0.6f, openAlpha_), HudAlign::Center);
}
