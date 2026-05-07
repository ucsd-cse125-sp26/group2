/// @file AmmoCounter.cpp
/// @brief Voidfall weapon panel implementation.

#include "AmmoCounter.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

namespace
{

const char* weaponName(int id)
{
    switch (id) {
    case 0:
        return "ARC-9";
    case 1:
        return "VOIDLANCE";
    case 2:
        return "PULSAR";
    case 3:
        return "FALL";
    default:
        return "WEAPON";
    }
}

const char* weaponFireMode(int id)
{
    switch (id) {
    case 0:
        return "AUTO";
    case 1:
        return "BOLT";
    case 2:
        return "RAIL";
    case 3:
        return "PULSE";
    default:
        return "—";
    }
}

/// @brief Hardcoded magazine size per weapon — matches the prototype tuning.
int weaponMagSize(int id)
{
    switch (id) {
    case 0:
        return 30;
    case 1:
        return 5;
    case 2:
        return 12;
    case 3:
        return 24;
    default:
        return 30;
    }
}

} // namespace

AmmoCounter::AmmoCounter()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -24.f;
    offsetY = -24.f;
    width = panelWidth;
    height = panelHeight;
}

void AmmoCounter::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
    displayClip_ = state.ammoClip;
    displayReserve_ = state.ammoReserve;
    weaponId_ = state.weaponId;
    displayMag_ = weaponMagSize(weaponId_);

    secondaryWeaponId_ = state.secondaryWeaponId;
    secondaryClip_ = state.secondaryClip;
    secondaryReserve_ = state.secondaryReserve;
}

void AmmoCounter::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float ph = panelHeight * s;

    // Anchor sits at bottom-right; align panel growing up-left.
    const float x = anchorX - pw;
    const float y = anchorY - ph;

    drawPanel(ctx, x, y, pw, ph, k_bgPanel, k_line, 1.f);
    drawCornerBrackets(ctx, x, y, pw, ph, 12.f * s, 1.f * s, 3.f * s, k_amber);

    const float padX = 14.f * s;
    const float padY = 10.f * s;

    // Header row: weapon name (left) + fire mode (right).
    const float nameFs = nameFontSize * s;
    const float fireFs = fireModeFontSize * s;
    ctx.text(weaponName(weaponId_), x + padX, y + padY, nameFs, k_textBright, HudAlign::Left);
    ctx.text(weaponFireMode(weaponId_),
             x + pw - padX,
             y + padY + (nameFs - fireFs) * 0.5f,
             fireFs,
             k_textDim,
             HudAlign::Right);

    // Hero ammo readout (right-aligned, large amber clip count).
    const float clipFs = clipFontSize * s;
    const float magFs = magFontSize * s;
    const float reserveFs = reserveFontSize * s;
    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%02d", displayClip_);

    char magText[16];
    SDL_snprintf(magText, sizeof(magText), "/%d", displayMag_);

    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "+%d", displayReserve_);

    const float clipBaselineY = y + padY + nameFs + 8.f * s;
    const float magBaselineY = clipBaselineY + (clipFs - magFs) * 0.55f;
    const float reserveBaselineY = clipBaselineY + (clipFs - reserveFs) * 0.55f;

    const float reserveW = ctx.measureText(reserveText, reserveFs);
    const float magW = ctx.measureText(magText, magFs);
    const float clipW = ctx.measureText(clipText, clipFs);

    const float rightX = x + pw - padX;
    // Right column from right-to-left: reserve, mag, clip.
    ctx.text(reserveText, rightX, reserveBaselineY, reserveFs, k_textDim, HudAlign::Right);
    const float magRight = rightX - reserveW - 10.f * s;
    ctx.text(magText, magRight, magBaselineY, magFs, k_textDim, HudAlign::Right);
    const float clipRight = magRight - magW - 4.f * s;
    ctx.text(clipText, clipRight, clipBaselineY, clipFs, k_amber, HudAlign::Right);
    (void)clipW;

    // Secondary slot (small line at the bottom).
    const float secY = y + ph - padY - secondaryFontSize * s - 1.f * s;
    const float hairY = secY - 6.f * s;
    ctx.rect(x + padX, hairY, pw - padX * 2.f, 1.f, k_lineDim);

    if (secondaryWeaponId_ >= 0) {
        // Key tab "[2]"
        drawKeyTab(ctx, "2", x + padX, secY, secondaryFontSize * s, 4.f * s, 1.f * s);
        const float keyW = ctx.measureText("2", secondaryFontSize * s) + 8.f * s;
        ctx.text(weaponName(secondaryWeaponId_),
                 x + padX + keyW + 8.f * s,
                 secY,
                 secondaryFontSize * s,
                 k_text,
                 HudAlign::Left);
        char secAmmo[24];
        SDL_snprintf(secAmmo, sizeof(secAmmo), "%d/%d", secondaryClip_, weaponMagSize(secondaryWeaponId_));
        ctx.text(secAmmo, x + pw - padX, secY, secondaryFontSize * s, k_textDim, HudAlign::Right);
    } else {
        ctx.text("[2] EMPTY", x + padX, secY, secondaryFontSize * s, k_textDim, HudAlign::Left);
    }
}
