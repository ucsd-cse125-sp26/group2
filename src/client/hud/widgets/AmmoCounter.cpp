/// @file AmmoCounter.cpp
/// @brief Minimal ammo readout implementation.

#include "AmmoCounter.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace
{
void drawAmmoSpark(HudContext& ctx, float cx, float cy, float size, HudColor color)
{
    const float arm = size * 0.5f;
    const float thick = std::max(1.f, size * 0.08f);
    ctx.rect(cx - thick * 0.5f, cy - arm, thick, arm * 2.f, color);
    ctx.rect(cx - arm, cy - thick * 0.5f, arm * 2.f, thick, color);
    ctx.rotatedRect(cx, cy, size * 0.20f, size * 0.82f, 45.f, color);
    ctx.rotatedRect(cx, cy, size * 0.20f, size * 0.82f, -45.f, color);
}
} // namespace

AmmoCounter::AmmoCounter()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -80.f;
    offsetY = -60.f;
    width = panelWidth;
    height = panelHeight;
}

void AmmoCounter::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
    displayClip_ = state.ammoClip;
    displayReserve_ = state.ammoReserve;
    weaponId_ = state.weaponId;
}

void AmmoCounter::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float ph = panelHeight * s;
    const float x = anchorX - pw;
    const float y = anchorY - ph;

    const float clipFs = clipFontSize * s;
    const float reserveFs = reserveFontSize * s;

    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%d", displayClip_);
    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "%d", displayReserve_);
    constexpr const char* slashText = "/";

    const HudColor accent = weaponTypeAccent(weaponId_);
    drawPanel(ctx, x, y, pw, ph, withAlpha(k_bgPanelSolid, 0.70f), k_lineBright, std::max(1.f, 1.5f * s));
    drawCornerBrackets(ctx, x, y, pw, ph, 20.f * s, std::max(1.f, s), 3.f * s, accent);

    const float innerPad = 13.f * s;
    ctx.gradientRect(x + innerPad,
                     y + innerPad,
                     pw - innerPad * 2.f,
                     ph - innerPad * 2.f,
                     withAlpha(k_quaternary, 0.50f),
                     withAlpha(k_infoBlue, 0.30f));
    ctx.rectOutline(x + innerPad,
                    y + innerPad,
                    pw - innerPad * 2.f,
                    ph - innerPad * 2.f,
                    std::max(1.f, s),
                    withAlpha(k_lineBright, 0.46f));

    const HudColor clipColor = k_textBright;
    const float reserveW = ctx.measureText(reserveText, reserveFs);
    const float slashW = ctx.measureText(slashText, reserveFs);
    const float rightX = x + pw - edgePadding * s - innerPad * 0.35f;
    const float centerY = y + ph * 0.5f;
    const float gap = 9.f * s;
    const float slashRight = rightX - reserveW - gap;
    const float clipRight = slashRight - slashW - gap;
    const float shadow = std::max(1.f, 2.f * s);

    ctx.text(clipText, clipRight + shadow, centerY - clipFs * 0.60f + shadow, clipFs, withAlpha(k_quaternary, 0.82f), HudAlign::Right);
    ctx.text(clipText, clipRight, centerY - clipFs * 0.64f, clipFs, clipColor, HudAlign::Right);
    ctx.text(slashText,
             slashRight + shadow,
             centerY - reserveFs * 0.34f + shadow,
             reserveFs,
             withAlpha(k_quaternary, 0.82f),
             HudAlign::Right);
    ctx.text(slashText, slashRight, centerY - reserveFs * 0.38f, reserveFs, k_textDim, HudAlign::Right);
    ctx.text(reserveText,
             rightX + shadow,
             centerY - reserveFs * 0.34f + shadow,
             reserveFs,
             withAlpha(k_quaternary, 0.82f),
             HudAlign::Right);
    ctx.text(reserveText, rightX, centerY - reserveFs * 0.38f, reserveFs, k_textDim, HudAlign::Right);

    drawAmmoSpark(ctx, x + pw - 34.f * s, y + ph * 0.50f, 42.f * s, withAlpha(k_textBright, 0.50f));
}
