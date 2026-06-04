/// @file AmmoCounter.cpp
/// @brief Minimal ammo readout implementation.

#include "AmmoCounter.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

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

    const float safeBgScale = std::max(0.01f, backgroundScale);
    const float safeBgStretchX = std::max(0.01f, backgroundStretchX);
    const float safeBgStretchY = std::max(0.01f, backgroundStretchY);
    const float bgW = panelWidth * s * safeBgScale * safeBgStretchX;
    const float bgH = panelHeight * s * safeBgScale * safeBgStretchY;
    const float bgX = x + backgroundOffsetX * s;
    const float bgY = y + backgroundOffsetY * s;

    ctx.svg(HudIcon::BulletCountBox, bgX, bgY, bgW, bgH);

    const float clipFs = clipFontSize * s;
    const float reserveFs = reserveFontSize * s;

    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%d", displayClip_);
    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "%d", displayReserve_);
    constexpr const char* slashText = "/";

    const HudColor clipColor = weaponTypeAccent(weaponId_);
    const float reserveW = ctx.measureText(reserveText, reserveFs);
    const float slashW = ctx.measureText(slashText, reserveFs);
    const float rightX = x + pw - edgePadding * s;
    const float centerY = y + ph * 0.5f;
    const float gap = 10.f * s;
    const float slashRight = rightX - reserveW - gap;
    const float clipRight = slashRight - slashW - gap;

    ctx.text(clipText, clipRight, centerY - clipFs * 0.64f, clipFs, clipColor, HudAlign::Right);
    ctx.text(slashText, slashRight, centerY - reserveFs * 0.38f, reserveFs, k_textDim, HudAlign::Right);
    ctx.text(reserveText, rightX, centerY - reserveFs * 0.38f, reserveFs, k_textDim, HudAlign::Right);
}
