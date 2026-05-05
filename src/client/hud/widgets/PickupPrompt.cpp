/// @file PickupPrompt.cpp
#include "PickupPrompt.hpp"

#include "hud/HudContext.hpp"

#include <SDL3/SDL.h>

namespace
{

/// @brief Convert a WeaponType integer (matches enum order in
///        ecs/components/WeaponState.hpp) to a human-readable label.
const char* weaponDisplayName(int weaponId)
{
    switch (weaponId) {
    case 0:
        return "Rifle";
    case 1:
        return "Rocket Launcher";
    case 2:
        return "Rail Gun";
    case 3:
        return "Energy Gun";
    default:
        return "Weapon";
    }
}

} // namespace

PickupPrompt::PickupPrompt()
{
    // Center horizontally, slightly below the crosshair so the prompt
    // doesn't fight the reticle for attention.
    anchor = HudAnchor::Center;
    offsetX = 0.f;
    offsetY = 110.f;
    visible = false;
}

void PickupPrompt::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive && state.pickupAvailable;
    weaponId_ = state.pickupWeaponId;
}

void PickupPrompt::draw(HudContext& ctx, float cx, float cy)
{
    const float s = uiScale_;
    const float fs = fontSize * s;
    const float kfs = keyFontSize * s;
    const float pad = keyBoxPadding * s;
    const float gap = spacing * s;
    const float radius = keyBoxRadius * s;

    const char* keyStr = "F";
    char prompt[64];
    SDL_snprintf(prompt, sizeof(prompt), "to pick up %s", weaponDisplayName(weaponId_));

    // Lay the prompt out as one horizontally-centered block:
    //
    //   [F]  to pick up Rocket Launcher
    //
    // The key glyph sits in a rounded rect on the left; the descriptive
    // text is left-aligned to its right.
    const float keyTextW = ctx.measureText(keyStr, kfs);
    const float boxW = keyTextW + pad * 2.f;
    const float boxH = kfs + pad * 2.f;
    const float promptW = ctx.measureText(prompt, fs);

    const float totalW = boxW + gap + promptW;
    const float startX = cx - totalW * 0.5f;
    const float midY = cy;

    // Backing pill behind the whole prompt for readability over busy scenes.
    const float bgPad = 10.f * s;
    const float bgX = startX - bgPad;
    const float bgY = midY - boxH * 0.5f - bgPad * 0.5f;
    const float bgW = totalW + bgPad * 2.f;
    const float bgH = boxH + bgPad;
    ctx.roundedRect(bgX, bgY, bgW, bgH, radius, HudColor(0.f, 0.f, 0.f, 0.55f));

    // Key glyph box (lighter rounded rect with the key letter centered).
    const float boxX = startX;
    const float boxY = midY - boxH * 0.5f;
    ctx.roundedRect(boxX, boxY, boxW, boxH, radius, HudColor(1.f, 1.f, 1.f, 0.18f));
    ctx.text(keyStr, boxX + boxW * 0.5f, boxY + (boxH - kfs) * 0.5f, kfs, HudColor::white(), HudAlign::Center);

    // Descriptive text to the right of the key glyph, vertically centered.
    const float textX = boxX + boxW + gap;
    const float textY = midY - fs * 0.5f;
    ctx.text(prompt, textX, textY, fs, HudColor::white(), HudAlign::Left);
}
