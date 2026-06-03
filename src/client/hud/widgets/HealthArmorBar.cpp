/// @file HealthArmorBar.cpp
/// @brief SVG-framed health and shield bar.

#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::TopCenter;
    offsetX = -250.f;
    offsetY = 98.f;
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
    const float safeScale = std::max(0.01f, svgScale);
    const float safeStretchX = std::max(0.01f, svgStretchX);
    const float safeStretchY = std::max(0.01f, svgStretchY);
    const float frameW = panelWidth * s * safeScale * safeStretchX;
    const float frameH = barHeight * s * safeScale * safeStretchY;
    const float frameX = x + svgOffsetX * s;
    const float frameY = y - frameH + svgOffsetY * s;

    width = panelWidth * safeScale * safeStretchX;
    height = barHeight * safeScale * safeStretchY;

    ctx.svg(HudIcon::HealthFrameBack, frameX, frameY, frameW, frameH);

    const float totalMax = static_cast<float>(maxHealth_ + maxArmor_);
    const float healthSegment = (static_cast<float>(maxHealth_) * healthFill_) / totalMax;
    const float shieldSegment = (static_cast<float>(maxArmor_) * armorFill_) / totalMax;
    const float healthRight = frameX + frameW * std::clamp(healthSegment, 0.f, 1.f);
    const float shieldRight = frameX + frameW * std::clamp(healthSegment + shieldSegment, 0.f, 1.f);

    if (healthRight > frameX) {
        ctx.pushClipRect(frameX, frameY, healthRight - frameX, frameH);
        ctx.svgMask(HudIcon::HealthFrameBack, frameX, frameY, frameW, frameH, k_health);
        ctx.popClipRect();
    }

    if (shieldRight > healthRight) {
        ctx.pushClipRect(healthRight, frameY, shieldRight - healthRight, frameH);
        ctx.svgMask(HudIcon::HealthFrameBack, frameX, frameY, frameW, frameH, k_cyan);
        ctx.popClipRect();
    }

    ctx.svg(HudIcon::HealthFrameFront, frameX, frameY, frameW, frameH);
}
