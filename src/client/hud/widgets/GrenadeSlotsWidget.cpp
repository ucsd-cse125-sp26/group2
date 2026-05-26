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
void drawCutCornerOutline(HudContext& ctx, float x, float y, float size, float cut, float thickness, HudColor color)
{
    const float c = std::clamp(cut, 0.f, size * 0.35f);
    const float vertices[] = {
        x + c,
        y,
        x + size - c,
        y,
        x + size,
        y + c,
        x + size,
        y + size - c,
        x + size - c,
        y + size,
        x + c,
        y + size,
        x,
        y + size - c,
        x,
        y + c,
    };
    const float pts[] = {
        vertices[0],
        vertices[1],
        vertices[2],
        vertices[3],
        vertices[4],
        vertices[5],
        vertices[6],
        vertices[7],
        vertices[8],
        vertices[9],
        vertices[10],
        vertices[11],
        vertices[12],
        vertices[13],
        vertices[14],
        vertices[15],
        x + c,
        y,
    };
    ctx.polyline(pts, 9, thickness, color);

    const float r = thickness * 0.5f;
    for (int i = 0; i < 8; ++i) {
        const float vx = vertices[i * 2 + 0];
        const float vy = vertices[i * 2 + 1];
        ctx.roundedRect(vx - r, vy - r, r * 2.f, r * 2.f, r, color);
    }
}

void drawSpacedText(HudContext& ctx, const char* text, float x, float y, float size, float gap, HudColor color)
{
    float cursor = x;
    for (const char* p = text; p && *p; ++p) {
        char ch[2] = {*p, '\0'};
        ctx.text(ch, cursor, y, size, color, HudAlign::Left, true);
        cursor += ctx.measureText(ch, size) + gap;
    }
}

void drawMolotovIcon(HudContext& ctx, float x, float y, float size, HudColor color)
{
    const float u = size / 16.f;
    ctx.rotatedRect(x + 8.f * u, y + 9.f * u, 5.f * u, 9.f * u, -18.f, color);
    ctx.rotatedRect(x + 6.2f * u, y + 3.8f * u, 3.f * u, 4.f * u, -18.f, color);
    ctx.triangle(x + 8.f * u, y + 0.5f * u, x + 11.f * u, y + 5.f * u, x + 6.f * u, y + 5.f * u, color);
}

void drawImpulseIcon(HudContext& ctx, float x, float y, float size, HudColor color)
{
    using namespace voidfall::icons;
    const float cx = x + size * 0.5f;
    const float cy = y + size * 0.54f;
    strokedCircle(ctx, cx, cy, size * 0.34f, std::max(1.f, size * 0.10f), 18, color);
    filledCircle(ctx, cx, cy, size * 0.11f, 10, color);
}

void drawGrenadeTypeIcon(HudContext& ctx, const std::string& name, float x, float y, float size, HudColor color)
{
    if (name == "MOLOTOV") {
        drawMolotovIcon(ctx, x, y, size, color);
    } else if (name == "IMPULSE") {
        drawImpulseIcon(ctx, x, y, size, color);
    } else {
        voidfall::icons::grenade(ctx, x, y, size, color);
    }
}
} // namespace

GrenadeSlotsWidget::GrenadeSlotsWidget()
{
    anchor = HudAnchor::TopLeft;
    offsetX = 184.f;
    offsetY = 28.f;
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

    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        const auto& item = state_.items[i];
        const float x = startX + static_cast<float>(i) * (ss + gap);
        const bool selected = static_cast<int>(i) == state_.selectedIndex;
        const bool available = item.available;
        const HudColor color = available ? (selected ? k_amber : k_textDim) : withAlpha(k_textDim, 0.38f);

        if (selected)
            drawCutCornerOutline(ctx, x, y, ss, cornerCut * s, std::max(1.f, borderThickness * s), k_lineBright);

        char count[8];
        SDL_snprintf(count, sizeof(count), "%d", item.count);
        drawSpacedText(ctx, count, x + countX, y + countY, fs, countGap, color);
        drawGrenadeTypeIcon(ctx, item.name, x + ss - icon - iconRight, y + (ss - icon) * 0.5f, icon, color);
    }
}
