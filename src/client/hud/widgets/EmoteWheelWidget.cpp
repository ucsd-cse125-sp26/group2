/// @file EmoteWheelWidget.cpp
/// @brief Radial emote-selection wheel.

#include "EmoteWheelWidget.hpp"

#include "ecs/components/EmoteCatalog.hpp"
#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/HudTween.hpp"
#include "hud/VoidfallStyle.hpp"

#include <cmath>

using namespace voidfall;

EmoteWheelWidget::EmoteWheelWidget()
{
    anchor = HudAnchor::Center;
    offsetX = 0.f;
    offsetY = 0.f;
    visible = false;
}

void EmoteWheelWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    state_ = state.emoteWheel;
    const bool show = state.isAlive && state_.open;

    const float targetAlpha = show ? 1.f : 0.f;
    if (std::abs(openAlpha_ - targetAlpha) > 0.01f)
        tweens.tween(&openAlpha_, targetAlpha, 0.12f, easeOutQuad);

    // Keep drawing during the fade-out so it doesn't pop.
    visible = show || openAlpha_ > 0.01f;
}

void EmoteWheelWidget::draw(HudContext& ctx, float drawX, float drawY)
{
    const float alpha = openAlpha_;
    if (alpha < 0.01f)
        return;

    const float s = uiScale_;
    const float cx = drawX;
    const float cy = drawY;
    const float r = radius * s;
    const float node = sectorRadius * s;
    const float labelFs = labelFontSize * s;
    const float titleFs = titleFontSize * s;

    // Dim backdrop ring so the wheel reads against busy scenes.
    icons::filledCircle(ctx, cx, cy, r + node, 48, withAlpha(k_quaternary, 0.55f * alpha));
    icons::strokedCircle(ctx, cx, cy, r + node, std::max(1.f, 1.5f * s), 48, withAlpha(k_lineDim, alpha));

    constexpr float k_twoPi = 6.28318530718f;
    const int count = emotes::kEmoteCount;

    for (int i = 0; i < count; ++i) {
        // Emote 0 at the top, proceeding clockwise (matches the input sampler).
        const float angle = static_cast<float>(i) * (k_twoPi / static_cast<float>(count));
        const float nx = cx + r * std::sin(angle);
        const float ny = cy - r * std::cos(angle);

        const bool selected = (i == state_.selectedIndex);
        const HudColor fill = selected ? withAlpha(k_primary, 0.92f * alpha) : withAlpha(k_quaternary, 0.92f * alpha);
        const HudColor border = selected ? withAlpha(k_amber, alpha) : withAlpha(k_lineBright, alpha);
        const HudColor textColor = selected ? withAlpha(k_textBright, alpha) : withAlpha(k_textDim, alpha);

        icons::filledCircle(ctx, nx, ny, node, 28, fill);
        icons::strokedCircle(ctx, nx, ny, node, std::max(1.f, (selected ? 2.5f : 1.5f) * s), 28, border);

        const char* label = emotes::emoteName(i);
        ctx.text(label, nx, ny - labelFs * 0.5f, labelFs, textColor, HudAlign::Center, true);
    }

    // Center caption: prompt, plus the highlighted emote name once one is picked.
    ctx.text("EMOTE", cx, cy - titleFs * 0.5f, titleFs, withAlpha(k_amber, alpha), HudAlign::Center, true);
    if (state_.selectedIndex >= 0) {
        ctx.text(emotes::emoteName(state_.selectedIndex),
                 cx,
                 cy + titleFs * 0.7f,
                 labelFs,
                 withAlpha(k_textBright, alpha),
                 HudAlign::Center,
                 true);
    }
}
