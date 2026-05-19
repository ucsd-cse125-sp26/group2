/// @file PickupPrompt.cpp
#include "PickupPrompt.hpp"

#include "config/InputBindings.hpp"
#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

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
    // Center horizontally, well below the crosshair so the prompt doesn't
    // sit in the player's primary aiming sightline.
    anchor = HudAnchor::Center;
    offsetX = 0.f;
    offsetY = 260.f;
    visible = false;
}

void PickupPrompt::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive && state.pickupAvailable;
    weaponId_ = state.pickupWeaponId;
    if (state.bindings) {
        keyLabel_ = InputBindings::bindingLabel(state.bindings->get(Action::Pickup));
    }
}

void PickupPrompt::draw(HudContext& ctx, float cx, float cy)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float fs = fontSize * s;
    const float kfs = keyFontSize * s;
    const float pad = keyBoxPadding * s;
    const float gap = spacing * s;

    const char* keyStr = keyLabel_.c_str();
    char prompt[64];
    SDL_snprintf(prompt, sizeof(prompt), "to pick up %s", weaponDisplayName(weaponId_));

    // Lay the prompt out as one horizontally-centered block:
    //
    //   [F]  to pick up Rocket Launcher
    //
    // The key glyph sits in a rounded rect on the left; the descriptive
    // text is left-aligned to its right.
    //
    // SDF font glyphs only fill ~72% of the EM size vertically (cap-height),
    // so basing the box on the font size yields a box ~2× too tall for the
    // visible key glyph. Hug the cap-height instead.
    const float capHeight = kfs * 0.72f;
    const float keyTextW = ctx.measureText(keyStr, kfs);
    const float boxW = keyTextW + pad * 2.f;
    const float boxH = capHeight + pad * 2.f;
    const float promptW = ctx.measureText(prompt, fs);

    const float totalW = boxW + gap + promptW;
    const float startX = cx - totalW * 0.5f;
    const float midY = cy;

    // Backing pill behind the whole prompt — Voidfall flat panel with amber
    // left-edge accent, no rounded corners.
    const float bgPadX = 12.f * s;
    const float bgPadY = 8.f * s;
    const float bgX = startX - bgPadX;
    const float bgY = midY - boxH * 0.5f - bgPadY;
    const float bgW = totalW + bgPadX * 2.f;
    const float bgH = boxH + bgPadY * 2.f;
    drawPanel(ctx, bgX, bgY, bgW, bgH, k_bgPanel, k_line, 1.f);
    // Amber left edge accent (4 px wide).
    ctx.rect(bgX, bgY, 2.f * s, bgH, k_amber);
    // Mil-spec corner brackets.
    drawCornerBrackets(ctx, bgX, bgY, bgW, bgH, 10.f * s, 1.f * s, 2.f * s, k_amber);

    // Key glyph box (lighter rect with the key letter centered).
    const float boxX = startX;
    const float boxY = midY - boxH * 0.5f;
    ctx.rect(boxX, boxY, boxW, boxH, HudColor{0.f, 0.f, 0.f, 0.40f});
    ctx.rectOutline(boxX, boxY, boxW, boxH, 1.f, k_lineBright);
    ctx.text(keyStr, boxX + boxW * 0.5f, midY - kfs * 0.64f, kfs, k_textBright, HudAlign::Center);

    // Descriptive text to the right of the key glyph.
    const float textX = boxX + boxW + gap;
    const float textY = midY - fs * 0.64f;
    ctx.text(prompt, textX, textY, fs, k_textBright, HudAlign::Left);
}
