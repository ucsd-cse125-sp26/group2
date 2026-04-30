/// @file CrosshairWidget.cpp
#include "CrosshairWidget.hpp"

#include "hud/HudContext.hpp"

CrosshairWidget::CrosshairWidget()
{
    anchor = HudAnchor::Center;
}

void CrosshairWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
}

void CrosshairWidget::draw(HudContext& ctx, float cx, float cy)
{
    const float s = uiScale_;
    const float gap = style.gap * s;
    const float len = style.length * s;
    const float t = style.thickness * s;
    const float ht = t * 0.5f;

    // Four arms.
    ctx.rect(cx + gap, cy - ht, len, t, style.color);       // right
    ctx.rect(cx - gap - len, cy - ht, len, t, style.color); // left
    ctx.rect(cx - ht, cy - gap - len, t, len, style.color); // top
    ctx.rect(cx - ht, cy + gap, t, len, style.color);       // bottom

    // Center dot.
    if (style.dot)
        ctx.rect(cx - ht, cy - ht, t, t, style.color);
}
