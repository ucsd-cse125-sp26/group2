/// @file ScreenDecalWidget.cpp
/// @brief Top and bottom screen decal HUD widgets.

#include "ScreenDecalWidget.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>

ScreenDecalWidget::ScreenDecalWidget(bool top) : top_(top)
{
    anchor = top_ ? HudAnchor::TopCenter : HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = 0.f;
    width = decalWidth;
    height = decalHeight;
}

TopDecalWidget::TopDecalWidget() : ScreenDecalWidget(true)
{
    decalWidth = 1920.f;
    decalHeight = 165.65f;
    decalScale = 0.9f;
    decalOffsetX = 1.f;
    decalOffsetY = 0.5f;
    decalStretchX = 1.f;
    decalStretchY = 0.56f;
    decalRotationDeg = 0.f;
    width = decalWidth * decalScale * decalStretchX;
    height = decalHeight * decalScale * decalStretchY;
}

BottomDecalWidget::BottomDecalWidget() : ScreenDecalWidget(false)
{
    offsetY = 37.f;
    decalWidth = 1800.f;
    decalHeight = 165.65f;
    decalScale = 0.9f;
    decalOffsetX = 0.f;
    decalOffsetY = 0.f;
    decalStretchX = 0.73f;
    decalStretchY = 1.35f;
    decalRotationDeg = 0.f;
    width = decalWidth * decalScale * decalStretchX;
    height = decalHeight * decalScale * decalStretchY;
}

void ScreenDecalWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
}

void ScreenDecalWidget::draw(HudContext& ctx, float anchorX, float anchorY)
{
    const float s = uiScale_;
    const float safeScale = std::max(0.01f, decalScale);
    const float safeStretchX = std::max(0.01f, decalStretchX);
    const float safeStretchY = std::max(0.01f, decalStretchY);
    const float w = decalWidth * s * safeScale * safeStretchX;
    const float h = decalHeight * s * safeScale * safeStretchY;
    const float x = anchorX - w * 0.5f + decalOffsetX * s;
    const float y = top_ ? anchorY + decalOffsetY * s : anchorY - h + decalOffsetY * s;
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;

    width = w / std::max(0.01f, s);
    height = h / std::max(0.01f, s);

    const std::size_t start = ctx.vertices().size();
    ctx.svg(top_ ? HudIcon::TopDecal : HudIcon::BottomDecal, x, y, w, h);
    ctx.rotateVertices(start, cx, cy, decalRotationDeg);
}
