/// @file EquippedWeaponsWidget.cpp
/// @brief Stacked primary/secondary weapon slot HUD widget.

#include "EquippedWeaponsWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

namespace
{
struct Rect
{
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
};

Rect tunedRect(float baseX,
               float baseY,
               float baseW,
               float baseH,
               const EquippedWeaponsWidget::SvgPartTuning& tuning,
               float uiScale)
{
    const float safeScale = std::max(0.01f, tuning.scale);
    const float safeStretchX = std::max(0.01f, tuning.stretchX);
    const float safeStretchY = std::max(0.01f, tuning.stretchY);
    const float w = baseW * safeScale * safeStretchX;
    const float h = baseH * safeScale * safeStretchY;
    return {
        baseX + (baseW - w) * 0.5f + tuning.offsetX * uiScale,
        baseY + (baseH - h) * 0.5f + tuning.offsetY * uiScale,
        w,
        h,
    };
}

HudIcon weaponIconForId(int weaponId)
{
    switch (weaponId) {
    case 0:
        return HudIcon::ARIcon;
    case 1:
        return HudIcon::RocketLauncherIcon;
    case 2:
        return HudIcon::RailGunIcon;
    case 3:
        return HudIcon::TrackingGunIcon;
    default:
        return HudIcon::NoIcon;
    }
}

void drawWeaponSlot(HudContext& ctx,
                    const EquippedWeaponsWidget::WeaponSlotTuning& tuning,
                    float baseX,
                    float baseY,
                    float frameW,
                    float frameH,
                    float iconW,
                    float iconH,
                    float uiScale,
                    int weaponId,
                    bool active)
{
    const Rect frame = tunedRect(baseX, baseY, frameW, frameH, tuning.frame, uiScale);
    const float iconBaseX = baseX + (frameW - iconW) * 0.5f;
    const float iconBaseY = baseY + (frameH - iconH) * 0.5f;
    const Rect icon = tunedRect(iconBaseX, iconBaseY, iconW, iconH, tuning.icon, uiScale);

    ctx.svg(HudIcon::WeaponFrame, frame.x, frame.y, frame.w, frame.h);
    const HudIcon weaponIcon = weaponIconForId(weaponId);
    if (active && weaponIcon != HudIcon::NoIcon)
        ctx.svgMask(weaponIcon, icon.x, icon.y, icon.w, icon.h, voidfall::k_cyan);
    else
        ctx.svgMask(weaponIcon, icon.x, icon.y, icon.w, icon.h, voidfall::k_textDim);
}
} // namespace

EquippedWeaponsWidget::EquippedWeaponsWidget()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -22.f;
    offsetY = -191.f;
    width = frameWidth;
    height = frameHeight * 2.f + frameGap;
}

void EquippedWeaponsWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
    primaryWeaponId_ = state.primaryWeaponId;
    secondaryWeaponId_ = state.secondarySlotWeaponId;
    activeWeaponSlot_ = state.activeWeaponSlot;
}

void EquippedWeaponsWidget::draw(HudContext& ctx, float anchorX, float anchorY)
{
    const float s = uiScale_;
    const float frameW = frameWidth * s;
    const float frameH = frameHeight * s;
    const float gap = frameGap * s;
    const float iconW = weaponIconWidth * s;
    const float iconH = weaponIconHeight * s;
    const float totalH = frameH * 2.f + gap;
    const float x = anchorX - frameW;
    const float y = anchorY - totalH;

    width = frameWidth;
    height = frameHeight * 2.f + frameGap;

    drawWeaponSlot(ctx, slots[0], x, y, frameW, frameH, iconW, iconH, s, primaryWeaponId_, activeWeaponSlot_ == 0);
    drawWeaponSlot(
        ctx, slots[1], x, y + frameH + gap, frameW, frameH, iconW, iconH, s, secondaryWeaponId_, activeWeaponSlot_ == 1);
}
