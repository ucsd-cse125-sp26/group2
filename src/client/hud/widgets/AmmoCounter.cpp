/// @file AmmoCounter.cpp
/// @brief Apex-style weapon cluster: type-color body + dual slot tabs.

#include "AmmoCounter.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <tuple>

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
    displayMag_ = state.magCapacity;

    secondaryWeaponId_ = state.secondaryWeaponId;
    secondaryClip_ = state.secondaryClip;
    secondaryReserve_ = state.secondaryReserve;
    secondaryMag_ = state.secondaryMagCapacity;
    secondaryKeybind_ = state.secondaryKeybind;
}

void AmmoCounter::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float ph = panelHeight * s;

    // Anchor at bottom-right; layout grows up-left.
    const float x = anchorX - pw;
    const float y = anchorY - ph;

    const HudColor typeC = weaponTypeAccent(weaponId_);

    const float tabH = slotTabHeight * s;
    const float bodyH = ph - tabH - 2.f * s; // -2px gap between body and tab row
    const float bodyY = y;
    const float tabY = y + bodyH + 2.f * s;

    // 1) Weapon body panel.
    drawPanel(ctx, x, bodyY, pw, bodyH, k_bgPanel, k_line, 1.f);
    drawCornerBrackets(ctx, x, bodyY, pw, bodyH, 12.f * s, 1.f * s, 3.f * s, k_amber);

    // 2) Type-color underline along the body's bottom edge.
    const float ulT = typeUnderlineThickness * s;
    ctx.rect(x, bodyY + bodyH - ulT, pw, ulT, typeC);

    const float padX = 14.f * s;
    const float padY = 10.f * s;

    // 3) Header row: weapon name + fire-mode tag (boxed in type color).
    const float nameFs = nameFontSize * s;
    const float fireFs = fireModeFontSize * s;
    ctx.text(weaponName(weaponId_), x + padX, bodyY + padY, nameFs, k_textBright, HudAlign::Left);

    const char* fireLabel = weaponFireMode(weaponId_);
    const float fireTextW = ctx.measureText(fireLabel, fireFs);
    const float fireBoxPad = 5.f * s;
    const float fireBoxH = fireFs + 4.f * s;
    const float fireBoxW = fireTextW + fireBoxPad * 2.f;
    const float fireBoxX = x + pw - padX - fireBoxW;
    const float fireBoxY = bodyY + padY + (nameFs - fireBoxH) * 0.5f;
    ctx.rectOutline(fireBoxX, fireBoxY, fireBoxW, fireBoxH, 1.f, typeC);
    ctx.text(fireLabel,
             fireBoxX + fireBoxW * 0.5f,
             fireBoxY + fireBoxH * 0.5f - fireFs * 0.64f + fireFs * 0.5f,
             fireFs,
             typeC,
             HudAlign::Center);

    // 4) Hero ammo readout (right-aligned, big mag count in type color).
    const float clipFs = clipFontSize * s;
    const float magFs = magFontSize * s;
    const float reserveFs = reserveFontSize * s;
    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%02d", displayClip_);
    char magText[16];
    SDL_snprintf(magText, sizeof(magText), "/%d", displayMag_);
    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "+%d", displayReserve_);

    const float clipBaselineY = bodyY + padY + nameFs + 8.f * s;
    const float magBaselineY = clipBaselineY + (clipFs - magFs) * 0.55f;
    const float reserveBaselineY = clipBaselineY + (clipFs - reserveFs) * 0.55f;

    const float reserveW = ctx.measureText(reserveText, reserveFs);
    const float magW = ctx.measureText(magText, magFs);

    const float rightX = x + pw - padX;
    ctx.text(reserveText, rightX, reserveBaselineY, reserveFs, k_textDim, HudAlign::Right);
    const float magRight = rightX - reserveW - 10.f * s;
    ctx.text(magText, magRight, magBaselineY, magFs, k_textDim, HudAlign::Right);
    const float clipRight = magRight - magW - 4.f * s;
    ctx.text(clipText, clipRight, clipBaselineY, clipFs, typeC, HudAlign::Right);

    // 5) Bottom row: two slot tabs side-by-side.  PRIMARY = slot 1,
    //    SECONDARY = slot 2.  The active slot is whichever isn't pointed
    //    at by `secondaryKeybind` (which advertises the *swap* key).
    //
    // Tab fields per slot:
    //   - weapon id  (drives type color + name)
    //   - clip count (right side of tab)
    //
    // We resolve "which slot has which weapon" from the current state by
    // mapping (active vs inactive) → (slot 1 vs slot 2).
    const int activeSlot = (secondaryKeybind_ == 1) ? 2 : 1;

    auto slotData = [&](int slotIdx) -> std::tuple<int, int, int> {
        // Returns (weaponId, clip, magCapacity) for the given slot index.
        const bool isActive = (slotIdx == activeSlot);
        if (isActive)
            return {weaponId_, displayClip_, displayMag_};
        return {secondaryWeaponId_, secondaryClip_, secondaryMag_};
    };

    const float tabGap = 2.f * s;
    const float tabW = (pw - tabGap) * 0.5f;

    auto drawTab = [&](int slotIdx) {
        const float tx = x + (slotIdx == 1 ? 0.f : tabW + tabGap);
        const auto [wid, clip, mag] = slotData(slotIdx);
        const bool isActive = (slotIdx == activeSlot);
        const bool empty = (wid < 0);
        const HudColor tabBg =
            isActive ? HudColor{0.13f, 0.125f, 0.115f, 0.95f} : HudColor{0.08f, 0.075f, 0.068f, 0.85f};
        const HudColor accent = empty ? k_lineDim : weaponTypeAccent(wid);

        // Tab background.
        ctx.rect(tx, tabY, tabW, tabH, tabBg);
        // Active tab: type-color top edge.  Inactive: dim hairline.
        const float topT = isActive ? 2.f * s : 1.f * s;
        ctx.rect(tx, tabY, tabW, topT, isActive ? accent : withAlpha(k_lineDim, 0.7f));

        // Index pill (filled with accent color, inverse text).
        const float pillFs = slotIndexFontSize * s;
        const char idxStr[2] = {static_cast<char>('0' + slotIdx), '\0'};
        const float pillTextW = ctx.measureText(idxStr, pillFs);
        const float pillPad = 4.f * s;
        const float pillH = pillFs + 4.f * s;
        const float pillW = pillTextW + pillPad * 2.f;
        const float pillX = tx + 8.f * s;
        const float pillY = tabY + (tabH - pillH) * 0.5f;
        const float pillAlpha = isActive ? 1.f : 0.55f;
        ctx.rect(pillX, pillY, pillW, pillH, withAlpha(accent, pillAlpha));
        // Index label in dark plate color so it reads "inverse" against
        // the accent fill.
        const HudColor idxC{0.06f, 0.055f, 0.05f, 1.f};
        ctx.text(idxStr,
                 pillX + pillW * 0.5f,
                 pillY + pillH * 0.5f - pillFs * 0.64f + pillFs * 0.5f,
                 pillFs,
                 idxC,
                 HudAlign::Center);

        // Weapon name (mid).
        const float slotFs = slotFontSize * s;
        const float nameX = pillX + pillW + 8.f * s;
        const float nameY = tabY + (tabH - slotFs) * 0.5f - slotFs * 0.18f;
        const HudColor nameC = empty ? withAlpha(k_textDim, 0.6f) : (isActive ? k_textBright : withAlpha(k_text, 0.7f));
        ctx.text(empty ? "EMPTY" : weaponName(wid), nameX, nameY, slotFs, nameC, HudAlign::Left);

        // Clip count (right).
        if (!empty) {
            char buf[16];
            SDL_snprintf(buf, sizeof(buf), "%d/%d", clip, mag);
            const HudColor numC = isActive ? k_textBright : withAlpha(k_textDim, 0.85f);
            ctx.text(buf, tx + tabW - 8.f * s, nameY, slotFs, numC, HudAlign::Right);
        }
    };

    drawTab(1);
    drawTab(2);
}
