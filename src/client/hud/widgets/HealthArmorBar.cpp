/// @file HealthArmorBar.cpp
/// @brief Voidfall vitals plate — Apex-style chamfered panel, segmented
///        shield, gradient HP, no numerics.

#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/HudTween.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::BottomLeft;
    offsetX = 24.f;
    offsetY = -28.f; // Sits ~28 px above the bottom edge so chrome breathes.
}

void HealthArmorBar::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;

    maxHealth_ = std::max(1, state.maxHealth);
    maxArmor_ = std::max(1, state.maxArmor);

    const int newHp = state.health;
    const int newAr = state.armor;

    const float targetHp = static_cast<float>(newHp) / static_cast<float>(maxHealth_);
    const float targetAr = static_cast<float>(newAr) / static_cast<float>(maxArmor_);
    const float liveLerp = std::clamp(dt * 12.f, 0.f, 1.f);
    liveHealth_ += (targetHp - liveHealth_) * liveLerp;
    liveArmor_ += (targetAr - liveArmor_) * liveLerp;

    if (newHp < displayHealth_) {
        trailHealth_ = static_cast<float>(displayHealth_) / static_cast<float>(maxHealth_);
        trailHpHoldTimer_ = trailHoldSeconds;
    } else if (newHp > displayHealth_) {
        trailHealth_ = targetHp;
    }
    if (newAr < displayArmor_) {
        trailArmor_ = static_cast<float>(displayArmor_) / static_cast<float>(maxArmor_);
        trailShHoldTimer_ = trailHoldSeconds;
    } else if (newAr > displayArmor_) {
        trailArmor_ = targetAr;
    }

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

namespace
{

/// @brief Render a chamfered (cut-bottom-right) gradient plate.
///
/// The CSS source is `clip-path: polygon(0 0, 100% 0, calc(100% - 14px)
/// 100%, 0 100%)` plus a horizontal three-stop gradient. We approximate
/// the gradient with two endpoint colors (left = dark+opaque, right = same
/// hue but more transparent) — the rasteriser interpolates per-vertex
/// across each triangle, giving a smooth fade without needing 3 stops.
void drawChamferedPlate(
    HudContext& ctx, float x, float y, float w, float h, float chamfer, HudColor leftColor, HudColor rightColor)
{
    using voidfall::lerpColor;

    // Pentagon vertices (TL → TR → R_mid → B_mid → BL):
    const float xTL = x;
    const float yTL = y;
    const float xTR = x + w;
    const float yTR = y;
    const float xRMid = x + w;
    const float yRMid = y + h - chamfer;
    const float xBMid = x + w - chamfer;
    const float yBMid = y + h;
    const float xBL = x;
    const float yBL = y + h;

    // Per-vertex color along the horizontal gradient.
    auto colorAt = [&](float vx) -> HudColor {
        const float t = std::clamp((vx - x) / w, 0.f, 1.f);
        return lerpColor(leftColor, rightColor, t);
    };
    const HudColor cTL = colorAt(xTL);
    const HudColor cTR = colorAt(xTR);
    const HudColor cRMid = colorAt(xRMid);
    const HudColor cBMid = colorAt(xBMid);
    const HudColor cBL = colorAt(xBL);

    // Triangulate as a fan from TL.
    ctx.triangleColors(xTL, yTL, cTL, xTR, yTR, cTR, xRMid, yRMid, cRMid);
    ctx.triangleColors(xTL, yTL, cTL, xRMid, yRMid, cRMid, xBMid, yBMid, cBMid);
    ctx.triangleColors(xTL, yTL, cTL, xBMid, yBMid, cBMid, xBL, yBL, cBL);
}

} // namespace

void HealthArmorBar::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float padX = panelPadX * s;
    const float padY = panelPadY * s;
    const float ch = chamferSize * s;
    const float shH = shieldBarHeight * s;
    const float hpH = healthBarHeight * s;
    const float gap = barSpacing * s;
    const float iconW = iconSize * s;
    const float iconG = iconGap * s;

    // Plate height covers the icon column + the two stacked bars + padding.
    const float plateH = padY * 2.f + shH + gap + hpH;

    // Anchor is bottom-left. Plate sits with its bottom edge at y, growing up.
    const float plateX = x;
    const float plateY = y - plateH;

    // 1) Chamfered gradient plate background.  Left = darker/opaque (k_bgVoid
    //    boosted), right = same hue with low alpha so it fades into the world.
    const HudColor plateLeft{0.06f, 0.055f, 0.05f, 0.92f};
    const HudColor plateRight{0.10f, 0.095f, 0.085f, 0.55f};
    drawChamferedPlate(ctx, plateX, plateY, pw, plateH, ch, plateLeft, plateRight);

    // 2) Amber accent stripe along the left edge.
    ctx.rect(plateX, plateY, 2.f * s, plateH, k_amber);

    // 3) Inside layout: icon column on the left, bars on the right.
    const float barX = plateX + padX + iconW + iconG;
    const float barW = pw - (padX * 2.f) - iconW - iconG;

    // Shield row (top).
    const float shRowY = plateY + padY;
    const float shIconY = shRowY + (shH - iconW) * 0.5f - 1.f * s;
    icons::shield(ctx, plateX + padX, shIconY, iconW, k_cyan);

    // Segmented shield bar — N segments with small gaps between them, each
    // its own gradient + ghost trail. Per-segment fill = clamp(total - i*max).
    const int segCount = std::max(1, shieldSegments);
    const float segGap = shieldSegmentGap * s;
    const float segW = (barW - segGap * static_cast<float>(segCount - 1)) / static_cast<float>(segCount);
    const float perSegMax = 1.f / static_cast<float>(segCount);
    for (int i = 0; i < segCount; ++i) {
        const float segX = barX + static_cast<float>(i) * (segW + segGap);
        // What fraction of *this* segment is filled? (live and trail)
        const float segLive = std::clamp((liveArmor_ - static_cast<float>(i) * perSegMax) / perSegMax, 0.f, 1.f);
        const float segTrail = std::clamp((trailArmor_ - static_cast<float>(i) * perSegMax) / perSegMax, 0.f, 1.f);
        drawGradientTrailBar(ctx, segX, shRowY, segW, shH, segLive, segTrail, k_cyanDim, k_cyan);
    }

    // HP row (bottom).
    const float hpRowY = shRowY + shH + gap;
    const float hpIconY = hpRowY + (hpH - iconW) * 0.5f;
    icons::hp(ctx, plateX + padX, hpIconY, iconW, k_red);
    drawGradientTrailBar(ctx,
                         barX,
                         hpRowY,
                         barW,
                         hpH,
                         std::clamp(liveHealth_, 0.f, 1.f),
                         std::clamp(trailHealth_, 0.f, 1.f),
                         k_red,
                         k_redBright);
}
