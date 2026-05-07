/// @file HealthArmorBar.cpp
/// @brief Voidfall vitals panel implementation.

#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/HudTween.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::BottomLeft;
    offsetX = 24.f;
    offsetY = -32.f; // Sits ~32 px above the bottom edge so chrome breathes.
    width = panelWidth;
    height = healthBarHeight + shieldBarHeight + barSpacing;
}

void HealthArmorBar::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;

    maxHealth_ = std::max(1, state.maxHealth);
    maxArmor_ = std::max(1, state.maxArmor);

    const int newHp = state.health;
    const int newAr = state.armor;

    // Snap live fills to current values (the live bar slides smoothly via its
    // CSS-equivalent-like fast tween: we just lerp toward target each frame).
    const float targetHp = static_cast<float>(newHp) / static_cast<float>(maxHealth_);
    const float targetAr = static_cast<float>(newAr) / static_cast<float>(maxArmor_);
    const float liveLerp = std::clamp(dt * 12.f, 0.f, 1.f);
    liveHealth_ += (targetHp - liveHealth_) * liveLerp;
    liveArmor_ += (targetAr - liveArmor_) * liveLerp;

    // Ghost trail: snap up to displayed value when no damage, hold then drain
    // back toward the live value when damage lands.
    if (newHp < displayHealth_) {
        trailHealth_ = static_cast<float>(displayHealth_) / static_cast<float>(maxHealth_);
        trailHpHoldTimer_ = trailHoldSeconds;
    } else if (newHp > displayHealth_) {
        // Health gain — snap trail up so we don't show a misleading drain.
        trailHealth_ = targetHp;
    }
    if (newAr < displayArmor_) {
        trailArmor_ = static_cast<float>(displayArmor_) / static_cast<float>(maxArmor_);
        trailShHoldTimer_ = trailHoldSeconds;
    } else if (newAr > displayArmor_) {
        trailArmor_ = targetAr;
    }

    // Decay holds, then drain trails toward the live fill.
    if (trailHpHoldTimer_ > 0.f)
        trailHpHoldTimer_ = std::max(0.f, trailHpHoldTimer_ - dt);
    if (trailShHoldTimer_ > 0.f)
        trailShHoldTimer_ = std::max(0.f, trailShHoldTimer_ - dt);

    if (trailHpHoldTimer_ <= 0.f && trailHealth_ > liveHealth_) {
        const float drainSpeed = 1.f / std::max(0.05f, trailDrainSeconds);
        trailHealth_ = std::max(liveHealth_, trailHealth_ - drainSpeed * dt);
    }
    if (trailShHoldTimer_ <= 0.f && trailArmor_ > liveArmor_) {
        const float drainSpeed = 1.f / std::max(0.05f, trailDrainSeconds);
        trailArmor_ = std::max(liveArmor_, trailArmor_ - drainSpeed * dt);
    }

    displayHealth_ = newHp;
    displayArmor_ = newAr;
}

void HealthArmorBar::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float shH = shieldBarHeight * s;
    const float hpH = healthBarHeight * s;
    const float gap = barSpacing * s;
    const float iconW = iconSize * s;
    const float iconG = iconGap * s;
    const float valueG = valueGap * s;
    const float hpFs = hpFontSize * s;
    const float shFs = shieldFontSize * s;

    // Anchor is bottom-left; layout the rows growing upward from y.  Compute
    // the top of the panel so the HP row (the largest one) sits flush with y.
    const float reservedValW = 50.f * s; // Right-aligned numeric column.
    const float barX = x + iconW + iconG;
    const float valueX = x + pw - 4.f * s;
    const float barW = (valueX - valueG - reservedValW) - barX;

    // Health row (bottom — main, larger).
    const float hpRowY = y - hpH;
    const float hpIconY = hpRowY + (hpH - iconW) * 0.5f;
    drawTrailBar(ctx,
                 barX,
                 hpRowY,
                 barW,
                 hpH,
                 std::clamp(liveHealth_, 0.f, 1.f),
                 std::clamp(trailHealth_, 0.f, 1.f),
                 k_red,
                 HudColor{0.95f, 0.95f, 0.95f, 0.45f});
    // HP icon: medical-cross glyph from the shared icon module.
    const float ix = x;
    const float iy = hpIconY;
    icons::hp(ctx, ix, iy, iconW, k_red);

    // Health numeral (right-aligned).
    char hpText[16];
    SDL_snprintf(hpText, sizeof(hpText), "%d", displayHealth_);
    ctx.text(hpText, valueX, hpRowY + (hpH - hpFs) * 0.5f - hpFs * 0.16f, hpFs, k_textBright, HudAlign::Right);

    // Shield row (above HP — thinner, cyan).
    const float shRowY = hpRowY - gap - shH;
    const float shIconY = shRowY + (shH - iconW) * 0.5f - 1.f * s;
    drawTrailBar(ctx,
                 barX,
                 shRowY,
                 barW,
                 shH,
                 std::clamp(liveArmor_, 0.f, 1.f),
                 std::clamp(trailArmor_, 0.f, 1.f),
                 k_cyan,
                 HudColor{0.95f, 0.95f, 0.95f, 0.45f});
    // Shield icon: heater-shield outline from the shared icon module.
    icons::shield(ctx, ix, shIconY - 1.f * s, iconW, k_cyan);

    // Shield numeral.
    char shText[16];
    SDL_snprintf(shText, sizeof(shText), "%d", displayArmor_);
    ctx.text(shText, valueX, shRowY + (shH - shFs) * 0.5f - shFs * 0.18f, shFs, k_cyan, HudAlign::Right);
}
