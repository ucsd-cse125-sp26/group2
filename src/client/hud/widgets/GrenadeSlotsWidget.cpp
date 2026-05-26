/// @file GrenadeSlotsWidget.cpp
/// @brief Three-slot grenade inventory row.

#include "GrenadeSlotsWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
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

HudColor grenadeColor(const std::string& name, bool selected, bool available)
{
    using namespace voidfall;
    if (!available)
        return withAlpha(k_textDim, 0.42f);
    if (selected)
        return k_yellow;
    if (name == "MOLOTOV")
        return k_amber;
    if (name == "IMPULSE")
        return k_cyan;
    return k_green;
}

} // namespace

GrenadeSlotsWidget::GrenadeSlotsWidget()
{
    anchor = HudAnchor::TopLeft;
    offsetX = 60.f;
    offsetY = 55.f;
    width = trayWidth;
    height = trayHeight;
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
    const float trayW = trayWidth * s;
    const float trayH = trayHeight * s;
    const float startX = anchorX + 28.f * s;
    const float y = anchorY + (trayH - ss) * 0.5f;
    const float icon = iconSize * s;
    const float fs = countFontSize * s;
    const float countX = countPadX * s;
    const float countY = countPadY * s;
    const float countGap = countCharacterGap * s;
    drawHoloPanel(ctx,
                  anchorX,
                  anchorY,
                  trayW,
                  trayH,
                  18.f * s,
                  withAlpha(k_bgPanelSolid, 0.52f),
                  withAlpha(k_bgPanel, 0.42f),
                  withAlpha(k_lineBright, 0.82f),
                  std::max(1.f, 2.f * s));

    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        const auto& item = state_.items[i];
        const float x = startX + static_cast<float>(i) * (ss + gap);
        const bool selected = static_cast<int>(i) == state_.selectedIndex;
        const bool available = item.available;
        const HudColor color = grenadeColor(item.name, selected, available);

        drawHoloPanel(ctx,
                      x,
                      y,
                      ss,
                      ss,
                      cornerCut * 4.f * s,
                      selected ? withAlpha(k_amberDeep, 0.22f) : withAlpha(k_bgPanel, 0.18f),
                      withAlpha(k_bgPanelSolid, 0.20f),
                      selected ? k_yellow : withAlpha(k_lineBright, 0.32f),
                      selected ? std::max(1.f, borderThickness * s * 0.35f) : std::max(1.f, s));
        if (selected) {
            drawCutCornerOutline(ctx, x, y, ss, cornerCut * 2.0f * s, std::max(1.f, borderThickness * s), k_yellow);
        }

        char count[8];
        SDL_snprintf(count, sizeof(count), "%d", item.count);
        ctx.icon(HudIcon::NoIcon, x + (ss - icon) * 0.5f, y + 12.f * s, icon, color);
        drawSpacedText(ctx, count, x + countX, y + ss - fs - countY, fs, countGap, k_textBright);
    }
}
