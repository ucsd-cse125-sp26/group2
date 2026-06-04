/// @file GrenadeSlotsWidget.cpp
/// @brief Three-slot grenade inventory row.

#include "GrenadeSlotsWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <string>

namespace
{
void drawSpacedText(HudContext& ctx, const char* text, float x, float y, float size, float gap, HudColor color)
{
    float cursor = x;
    for (const char* p = text; p && *p; ++p) {
        char ch[2] = {*p, '\0'};
        ctx.text(ch, cursor, y, size, color, HudAlign::Left, true);
        cursor += ctx.measureText(ch, size) + gap;
    }
}

void drawGrenadeTypeIcon(HudContext& ctx, const std::string& name, float x, float y, float size, HudColor color)
{
    if (name == "MOLOTOV") {
        ctx.svg(HudIcon::MolotovGrenadeIcon, x, y, size, size, color);
    } else if (name == "STICKY") {
        ctx.svg(HudIcon::StickyGrenadeIcon, x, y, size, size, color);
    } else {
        ctx.svg(HudIcon::FragGrenadeIcon, x, y, size, size, color);
    }
}
} // namespace

GrenadeSlotsWidget::GrenadeSlotsWidget()
{
    anchor = HudAnchor::TopLeft;
    offsetX = 220.f;
    offsetY = 59.f;
    width = slotSize * static_cast<float>(kHudGrenadeSlots) + slotGap * static_cast<float>(kHudGrenadeSlots - 1);
    height = slotSize;
}

void GrenadeSlotsWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    state_ = state.grenadeRadial;
    visible = state.isAlive;
}

void GrenadeSlotsWidget::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float ss = slotSize * s;
    const float gap = slotGap * s;
    const float totalW = ss * static_cast<float>(kHudGrenadeSlots) + gap * static_cast<float>(kHudGrenadeSlots - 1);
    const float startX = anchorX - totalW * 0.5f;
    const float y = anchorY;
    const float icon = iconSize * s;
    const float fs = countFontSize * s;
    const float countX = countPadX * s;
    const float countY = countPadY * s;
    const float countGap = countCharacterGap * s;
    const float iconRight = iconPadRight * s;
    const float safeBgScale = std::max(0.01f, backgroundScale);
    const float safeBgStretchX = std::max(0.01f, backgroundStretchX);
    const float safeBgStretchY = std::max(0.01f, backgroundStretchY);
    const float bgW = backgroundWidth * s * safeBgScale * safeBgStretchX;
    const float bgH = backgroundHeight * s * safeBgScale * safeBgStretchY;
    const float bgX = anchorX + backgroundOffsetX * s;
    const float bgY = anchorY + backgroundOffsetY * s;
    const float bgCx = bgX + bgW * 0.5f;
    const float bgCy = bgY + bgH * 0.5f;

    const std::size_t backgroundStart = ctx.vertices().size();
    ctx.svg(HudIcon::GrenadeBox, bgX, bgY, bgW, bgH);
    ctx.rotateVertices(backgroundStart, bgCx, bgCy, backgroundRotationDeg);

    const std::size_t componentStart = ctx.vertices().size();

    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        const auto& item = state_.items[i];
        const float x = startX + static_cast<float>(i) * (ss + gap);
        const bool selected = static_cast<int>(i) == state_.selectedIndex;
        const bool available = item.available;
        const HudColor color = available ? (selected ? k_amber : k_textDim) : withAlpha(k_textDim, 0.38f);

        char count[8];
        SDL_snprintf(count, sizeof(count), "%d", item.count);
        drawSpacedText(ctx, count, x + countX, y + countY, fs, countGap, color);
        drawGrenadeTypeIcon(ctx, item.name, x + ss - icon - iconRight, y + (ss - icon) * 0.5f, icon, color);
    }

    ctx.rotateVertices(componentStart, startX + totalW * 0.5f, y + ss * 0.5f, elementsRotationDeg);
}
