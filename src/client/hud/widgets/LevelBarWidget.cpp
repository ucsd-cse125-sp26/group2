/// @file LevelBarWidget.cpp
/// @brief Ability level progress bar below the ability slots.

#include "LevelBarWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

LevelBarWidget::LevelBarWidget()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -28.f;
    width = barWidth;
    height = barHeight;
}

void LevelBarWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
    progress_ = std::clamp(state.abilityLevelProgress, 0.f, 1.f);
}

void LevelBarWidget::draw(HudContext& ctx, float anchorX, float anchorY)
{
    const float s = uiScale_;
    const float safeScale = std::max(0.01f, svgScale);
    const float safeStretchX = std::max(0.01f, svgStretchX);
    const float safeStretchY = std::max(0.01f, svgStretchY);
    const float w = barWidth * s * safeScale * safeStretchX;
    const float h = barHeight * s * safeScale * safeStretchY;
    const float x = anchorX - w * 0.5f + svgOffsetX * s;
    const float y = anchorY - h + svgOffsetY * s;
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;

    width = w / std::max(0.01f, s);
    height = h / std::max(0.01f, s);

    const std::size_t start = ctx.vertices().size();
    ctx.svg(HudIcon::LevelBarBack, x, y, w, h);
    ctx.svgMaskPartialX(HudIcon::LevelBarBack, x, y, w, h, progress_, voidfall::k_primary);
    ctx.svg(HudIcon::LevelBarFront, x, y, w, h);
    ctx.rotateVertices(start, cx, cy, svgRotationDeg);
}
