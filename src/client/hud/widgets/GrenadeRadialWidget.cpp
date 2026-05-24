/// @file GrenadeRadialWidget.cpp
/// @brief Held-G grenade selection radial.

#include "GrenadeRadialWidget.hpp"

#include "config/InputBindings.hpp"
#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <array>
#include <cmath>
#include <cstdio>

GrenadeRadialWidget::GrenadeRadialWidget()
{
    anchor = HudAnchor::Center;
    offsetX = 0.0f;
    offsetY = -20.0f;
    visible = false;
}

void GrenadeRadialWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    state_ = state.grenadeRadial;
    if (state.bindings) {
        keyLabel_ = InputBindings::bindingLabel(state.bindings->get(Action::CycleGrenade));
    }
    visible = state.isAlive && state_.open;
}

namespace
{

constexpr std::array<float, kHudGrenadeSlots * 2> k_dirs = {
    0.0f,
    -1.0f,
    0.8660254f,
    0.5f,
    -0.8660254f,
    0.5f,
};

void drawKeyTag(HudContext& ctx, const char* label, float x, float y, float fs)
{
    using namespace voidfall;
    const float padX = 8.0f;
    const float w = std::max(26.0f, ctx.measureText(label, fs) + padX * 2.0f);
    const float h = 20.0f;
    ctx.rect(x - w * 0.5f, y - h * 0.5f, w, h, withAlpha(k_quaternary, 0.58f));
    ctx.rectOutline(x - w * 0.5f, y - h * 0.5f, w, h, 1.0f, k_amber);
    ctx.text(label, x, y - fs * 0.42f, fs, k_amber, HudAlign::Center, true);
}

} // namespace

void GrenadeRadialWidget::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float r = radius * s;
    const float cardW = cardWidth * s;
    const float cardH = cardHeight * s;
    const float nameFs = nameFontSize * s;
    const float countFs = countFontSize * s;
    const float keyFs = keyFontSize * s;

    drawKeyTag(ctx, keyLabel_.c_str(), anchorX, anchorY, keyFs);

    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        const float dx = k_dirs[i * 2] * r;
        const float dy = k_dirs[i * 2 + 1] * r;
        const float cx = anchorX + dx;
        const float cy = anchorY + dy;
        const float x = cx - cardW * 0.5f;
        const float y = cy - cardH * 0.5f;
        const bool selected = static_cast<int>(i) == state_.selectedIndex;
        const bool available = state_.items[i].available;
        const HudColor border = selected ? k_amber : (available ? k_lineBright : k_lineDim);
        const HudColor fill = selected ? withAlpha(k_secondary, 0.90f) : withAlpha(k_quaternary, 0.84f);
        const HudColor nameColor = available ? (selected ? k_amber : k_textBright) : withAlpha(k_textDim, 0.50f);
        const HudColor countColor = available ? k_textDim : withAlpha(k_textDim, 0.45f);

        const float line[4] = {anchorX, anchorY, cx, cy};
        ctx.polyline(line, 2, selected ? 2.0f * s : 1.0f * s, border);
        drawPanel(ctx, x, y, cardW, cardH, fill, border, selected ? 2.0f : 1.0f);
        drawCornerBrackets(ctx, x, y, cardW, cardH, 8.0f * s, 1.0f, 1.5f * s, border);

        ctx.text(state_.items[i].name.c_str(), cx, y + 8.0f * s, nameFs, nameColor, HudAlign::Center, true);

        char count[16];
        std::snprintf(count, sizeof(count), "%d", state_.items[i].count);
        ctx.text(count, cx, y + 26.0f * s, countFs, countColor, HudAlign::Center, true);
    }
}
