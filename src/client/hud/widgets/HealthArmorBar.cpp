/// @file HealthArmorBar.cpp
/// @brief SVG-framed health and shield bar.

#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

namespace
{
constexpr HudColor k_healthFill{1.0f, 0.35f, 0.32f, 1.0f};
constexpr HudColor k_overShieldFill{0.18f, 1.0f, 0.34f, 1.0f};
constexpr HudColor k_damagePowerupBorder{0.20f, 0.62f, 1.0f, 1.0f};
}

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::TopCenter;
    offsetX = -315.f;
    offsetY = 82.f;
    width = panelWidth;
    height = barHeight;
}

void HealthArmorBar::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;

    maxHealth_ = std::max(1, state.maxHealth);
    maxArmor_ = std::max(1, state.maxArmor);
    maxOverShield_ = std::max(1, state.maxOverShield);
    healthFill_ = std::clamp(static_cast<float>(state.health) / static_cast<float>(maxHealth_), 0.f, 1.f);
    armorFill_ = std::clamp(static_cast<float>(state.armor) / static_cast<float>(maxArmor_), 0.f, 1.f);
    overShieldFill_ =
        std::clamp(static_cast<float>(state.overShield) / static_cast<float>(maxOverShield_), 0.f, 1.f);
    damagePowerupFill_ = std::clamp(state.damagePowerupProgress, 0.f, 1.f);
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

    ctx.svgMaskRangeX(HudIcon::HealthFrameBack, frameX, frameY, frameW, frameH, 0.f, healthFill_, k_healthFill);
    ctx.svgMaskRangeX(HudIcon::HealthFrameBack, frameX, frameY, frameW, frameH, 0.f, armorFill_, k_primary);
    ctx.svgMaskRangeX(HudIcon::HealthFrameBack, frameX, frameY, frameW, frameH, 0.f, overShieldFill_, k_overShieldFill);

    ctx.svg(HudIcon::HealthFrameFront, frameX, frameY, frameW, frameH);
    if (damagePowerupFill_ > 0.f)
        ctx.svgMaskRangeX(
            HudIcon::HealthFrameFront, frameX, frameY, frameW, frameH, 0.f, damagePowerupFill_, k_damagePowerupBorder);
}
