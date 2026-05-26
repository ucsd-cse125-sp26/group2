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

void drawWeaponSlot(HudContext& ctx, float x, float y, float w, float h, float s, int key, bool active)
{
    using namespace voidfall;
    const HudColor border = active ? k_primary : withAlpha(k_textDim, 0.38f);
    const HudColor iconColor = active ? k_textBright : withAlpha(k_textDim, 0.72f);
    drawHoloPanel(ctx,
                  x,
                  y,
                  w,
                  h,
                  22.f * s,
                  active ? withAlpha(k_infoBlue, 0.24f) : withAlpha(k_bgPanelSolid, 0.42f),
                  withAlpha(k_bgPanel, active ? 0.40f : 0.30f),
                  border,
                  (active ? 2.5f : 1.5f) * s);
    char keyText[4];
    SDL_snprintf(keyText, sizeof(keyText), "%d", key);
    drawKeyTab(ctx,
               keyText,
               x - 58.f * s,
               y + h * 0.5f - 22.f * s,
               34.f * s,
               10.f * s,
               5.f * s,
               withAlpha(k_bgPanelSolid, 0.72f),
               border,
               k_textBright);
    const float icon = h * 0.62f;
    ctx.icon(HudIcon::NoIcon, x + (w - icon) * 0.5f, y + (h - icon) * 0.5f, icon, iconColor);
}
} // namespace

AmmoCounter::AmmoCounter()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -55.f;
    offsetY = -40.f;
    width = panelWidth;
    height = panelHeight;
}

void AmmoCounter::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
    displayClip_ = state.ammoClip;
    displayReserve_ = state.ammoReserve;
    weaponId_ = state.weaponId;
    secondaryKeybind_ = state.secondaryKeybind;
}

void AmmoCounter::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float ph = panelHeight * s;
    const float x = anchorX - pw;
    const float y = anchorY - ph;
    const float slotW = weaponSlotWidth * s;
    const float slotH = weaponSlotHeight * s;
    const float slotGap = weaponSlotGap * s;
    const float slotBottomGap = weaponSlotBottomGap * s;

    const float slot2Y = y - slotBottomGap - slotH;
    const float slot1Y = slot2Y - slotGap - slotH;
    drawWeaponSlot(ctx, x, slot1Y, slotW, slotH, s, secondaryKeybind_ == 1 ? 1 : 1, false);
    drawWeaponSlot(ctx, x, slot2Y, slotW, slotH, s, 2, true);

    const float clipFs = clipFontSize * s;
    const float reserveFs = reserveFontSize * s;

    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%d", displayClip_);
    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "%d", displayReserve_);
    constexpr const char* slashText = "/";

    const HudColor accent = weaponTypeAccent(weaponId_);
    drawHoloPanel(ctx,
                  x,
                  y,
                  pw,
                  ph,
                  25.f * s,
                  withAlpha(k_bgPanelSolid, 0.74f),
                  withAlpha(k_bgPanel, 0.48f),
                  k_lineBright,
                  std::max(1.f, 2.5f * s));
    drawCornerBrackets(ctx, x, y, pw, ph, 30.f * s, std::max(1.f, 1.5f * s), 3.f * s, accent);

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

    drawAmmoSpark(ctx, x + pw - 38.f * s, y + ph * 0.50f, 56.f * s, withAlpha(k_textDim, 0.48f));
}
