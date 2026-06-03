/// @file EquipmentSlots.cpp
/// @brief Bottom-center two-ability SVG cooldown widget.

#include "EquipmentSlots.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace
{

struct Rect
{
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
};

enum class AbilitySide
{
    Left = 0,
    Right = 1,
};

HudIcon iconForAbility(std::string_view name, bool available)
{
    if (!available || name == "LOCKED")
        return HudIcon::NoIcon;
    if (name == "GRAPPLE")
        return HudIcon::Grapple;
    if (name == "GRAVITY")
        return HudIcon::Gravity;
    if (name == "DASH" || name == "RECALL")
        return HudIcon::Tactical;
    return HudIcon::NoIcon;
}

Rect tunedRect(float baseX,
               float baseY,
               float baseW,
               float baseH,
               const EquipmentSlots::SvgComponentTuning& tuning,
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

void drawCooldownBar(HudContext& ctx, const Rect& bar, AbilitySide side, float charge, bool flipX, bool flipY)
{
    constexpr HudColor kCooldownRed{1.f, 0.f, 0.f, 1.f};
    const bool flipBarX = (side == AbilitySide::Right) != flipX;
    const float remaining = std::clamp(1.f - charge, 0.f, 1.f);

    ctx.svgFlipped(HudIcon::AbilityBarBack, bar.x, bar.y, bar.w, bar.h, flipBarX, flipY);

    if (remaining > 0.f) {
        const float fillW = bar.w * remaining;
        const float clipX = side == AbilitySide::Left ? bar.x : bar.x + bar.w - fillW;
        ctx.pushClipRect(clipX, bar.y, fillW, bar.h);
        ctx.svgMaskFlipped(HudIcon::AbilityBarBack, bar.x, bar.y, bar.w, bar.h, flipBarX, flipY, kCooldownRed);
        ctx.popClipRect();
    }

    ctx.svgFlipped(HudIcon::AbilityBarFront, bar.x, bar.y, bar.w, bar.h, flipBarX, flipY);
}

void drawAbilityElement(HudContext& ctx,
                        const EquipmentSlots::AbilityElementTuning& tuning,
                        float frameBaseX,
                        float frameBaseY,
                        float barBaseX,
                        float barBaseY,
                        float frameW,
                        float frameH,
                        float iconSize,
                        float barW,
                        float barH,
                        float uiScale,
                        AbilitySide side,
                        HudIcon abilityIcon,
                        float charge)
{
    const Rect frame = tunedRect(frameBaseX, frameBaseY, frameW, frameH, tuning.iconFrame, uiScale);
    const float iconBaseX = frameBaseX + (frameW - iconSize) * 0.5f;
    const float iconBaseY = frameBaseY + (frameH - iconSize) * 0.5f;
    const Rect icon = tunedRect(iconBaseX, iconBaseY, iconSize, iconSize, tuning.icon, uiScale);
    const Rect bar = tunedRect(barBaseX, barBaseY, barW, barH, tuning.bar, uiScale);

    ctx.svg(HudIcon::AbilityIconFrame, frame.x, frame.y, frame.w, frame.h);
    ctx.svgFlipped(abilityIcon, icon.x, icon.y, icon.w, icon.h, tuning.flipIconX, tuning.flipIconY);
    drawCooldownBar(ctx, bar, side, charge, tuning.flipBarX, tuning.flipBarY);
}

} // namespace

EquipmentSlots::EquipmentSlots()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -40.f;
    width = iconFrameWidth * 2.f + barWidth * 2.f + iconBarGap * 2.f + centerGap;
    height = std::max(iconFrameHeight, barHeight);
}

void EquipmentSlots::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    state_ = state.equipment;
    visible = state.isAlive;
}

void EquipmentSlots::draw(HudContext& ctx, float anchorX, float anchorY)
{
    const float s = uiScale_;
    const float frameW = iconFrameWidth * s;
    const float frameH = iconFrameHeight * s;
    const float iconSize = abilityIconSize * s;
    const float barW = barWidth * s;
    const float barH = barHeight * s;
    const float gap = iconBarGap * s;
    const float midGap = centerGap * s;

    const float totalW = frameW * 2.f + barW * 2.f + gap * 2.f + midGap;
    const float rowH = std::max(frameH, barH);
    const float startX = anchorX - totalW * 0.5f;
    const float topY = anchorY - rowH;
    const float frameY = topY + (rowH - frameH) * 0.5f;
    const float barY = topY + (rowH - barH) * 0.5f;

    const float leftFrameX = startX;
    const float leftBarX = leftFrameX + frameW + gap;
    const float rightBarX = leftBarX + barW + midGap;
    const float rightFrameX = rightBarX + barW + gap;

    width = totalW / std::max(0.01f, s);
    height = rowH / std::max(0.01f, s);

    drawAbilityElement(ctx,
                       abilityElements[0],
                       leftFrameX,
                       frameY,
                       leftBarX,
                       barY,
                       frameW,
                       frameH,
                       iconSize,
                       barW,
                       barH,
                       s,
                       AbilitySide::Left,
                       iconForAbility(state_.primaryAbilityName, state_.primaryAbilityAvailable),
                       state_.primaryAbilityCharge);

    drawAbilityElement(ctx,
                       abilityElements[1],
                       rightFrameX,
                       frameY,
                       rightBarX,
                       barY,
                       frameW,
                       frameH,
                       iconSize,
                       barW,
                       barH,
                       s,
                       AbilitySide::Right,
                       iconForAbility(state_.secondaryAbilityName, state_.secondaryAbilityAvailable),
                       state_.secondaryAbilityCharge);
}
