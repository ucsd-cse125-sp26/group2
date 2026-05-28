/// @file HealthArmorBar.cpp
/// @brief Layered health and armor silhouette.

#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

namespace
{
void drawFillEdge(HudContext& ctx,
                  float x,
                  float y,
                  float w,
                  float h,
                  float fill01,
                  float thickness,
                  HudColor color)
{
    const float fill = std::clamp(fill01, 0.f, 1.f);
    if (fill <= 0.01f)
        return;

    const float fillX = x + w * fill;
    const float edgeH = h * 0.82f;
    const float edgeY = y + (h - edgeH) * 0.5f;
    ctx.rect(fillX - thickness * 0.5f,
             edgeY,
             thickness,
             edgeH,
             voidfall::withAlpha(color, fill < 0.995f ? 0.90f : 0.55f));
    ctx.rect(fillX - thickness * 1.5f,
             edgeY,
             thickness * 3.f,
             edgeH,
             voidfall::withAlpha(color, fill < 0.995f ? 0.22f : 0.12f));
}
} // namespace

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::TopCenter;
    offsetX = -325.f;
    offsetY = 92.f;
    width = panelWidth;
    height = barHeight;
}

void HealthArmorBar::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;

    maxHealth_ = std::max(1, state.maxHealth);
    maxArmor_ = std::max(1, state.maxArmor);
    healthFill_ = std::clamp(static_cast<float>(state.health) / static_cast<float>(maxHealth_), 0.f, 1.f);
    armorFill_ = std::clamp(static_cast<float>(state.armor) / static_cast<float>(maxArmor_), 0.f, 1.f);
}

void HealthArmorBar::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float w = panelWidth * s;
    const float h = barHeight * s;
    const float outline = std::max(1.f, outlineThickness * s);
    const float topY = y - h;
    const float inset = outline * 1.5f;
    const float innerX = x + inset;
    const float innerY = topY + inset;
    const float innerW = w - inset * 2.f;
    const float innerH = h - inset * 2.f;
    const float centerX = x + w * 0.5f + visorOffsetX * s;

    const float visorW = visorWidth * s;
    const float visorLeft = centerX - visorW * 0.5f;
    const float visorRight = centerX + visorW * 0.5f;
    const float visorY = std::max(0.f, visorTopY * s);
    const float frameY = topY + h * visorFrameRatio + visorFrameOffsetY * s;
    const float wingInset = visorWingInset * s;
    const float innerGap = visorInnerGap * s;
    const float leftFrame[] = {
        visorLeft,
        visorY,
        visorLeft + wingInset,
        frameY,
        x - innerGap,
        frameY,
    };
    const float rightFrame[] = {
        x + w + innerGap,
        frameY,
        visorRight - wingInset,
        frameY,
        visorRight,
        visorY,
    };
    const float mainT = std::max(0.1f, visorThickness * s);
    const float coreT = std::max(0.1f, visorCoreThickness * s);
    auto drawVisorWing = [&](const float* points) {
        ctx.polyline(points, 3, std::max(1.f, mainT * 3.2f), withAlpha(k_primary, 0.08f));
        ctx.polyline(points, 3, std::max(1.f, mainT * 1.6f), withAlpha(k_primary, 0.20f));
        ctx.polyline(points, 3, mainT, withAlpha(k_tertiary, 0.72f));
        ctx.polyline(points, 3, coreT, k_primary);
    };
    drawVisorWing(leftFrame);
    drawVisorWing(rightFrame);

    const float tabW = 52.f * s;
    const float tabGap = 10.f * s;
    const float tabY = topY - 24.f * s;
    ctx.rect(centerX - tabW - tabGap, tabY, tabW, std::max(2.f, 4.f * s), withAlpha(k_tertiary, 0.54f));
    ctx.rect(centerX + tabGap, tabY, tabW, std::max(2.f, 4.f * s), withAlpha(k_tertiary, 0.54f));

    ctx.iconRect(HudIcon::NoIcon, x, topY, w, h, withAlpha(k_bgInset, 0.82f));
    ctx.iconRect(HudIcon::NoIcon, x - outline, topY - outline, w + outline * 2.f, h + outline * 2.f, withAlpha(k_primary, 0.08f));
    ctx.iconRect(HudIcon::NoIcon, x, topY, w, h, withAlpha(k_lineBright, 0.42f));

    ctx.iconFillRect(HudIcon::NoIcon, innerX, innerY, innerW, innerH, healthFill_, k_redBright);
    drawFillEdge(ctx, innerX, innerY, innerW, innerH, healthFill_, std::max(2.f, outline * 0.55f), k_textBright);

    if (armorFill_ > 0.01f) {
        ctx.iconFillRect(HudIcon::NoIcon, innerX, innerY, innerW, innerH, armorFill_, k_healthBright);
        drawFillEdge(ctx, innerX, innerY, innerW, innerH, armorFill_, std::max(2.f, outline * 0.55f), k_healthBright);
    }
}
