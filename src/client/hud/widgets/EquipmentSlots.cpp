/// @file EquipmentSlots.cpp
/// @brief Bottom-center two-ability SVG cooldown widget.

#include "EquipmentSlots.hpp"

#include "config/InputBindings.hpp"
#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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

std::string compactBindingLabel(std::string label)
{
    if (label.empty() || label == "Unbound")
        return "-";
    if (label.size() == 1)
        return label;

    if (label == "Left Shift")
        return "SHFT";
    if (label == "Right Shift")
        return "RSHF";
    if (label == "Left Ctrl")
        return "CTRL";
    if (label == "Right Ctrl")
        return "RCTRL";
    if (label == "Left Alt")
        return "ALT";
    if (label == "Right Alt")
        return "RALT";
    if (label == "Space")
        return "SPC";
    if (label == "Return")
        return "RET";
    if (label == "Backspace")
        return "BKSP";
    if (label == "Tab")
        return "TAB";
    if (label == "Escape")
        return "ESC";
    if (label == "Mouse Left")
        return "MB1";
    if (label == "Mouse Right")
        return "MB2";
    if (label == "Mouse Middle")
        return "MB3";
    if (label == "Mouse X1")
        return "M4";
    if (label == "Mouse X2")
        return "M5";
    if (label == "Mouse Wheel Up")
        return "MWU";
    if (label == "Mouse Wheel Down")
        return "MWD";
    if (label == "Gamepad South")
        return "A";
    if (label == "Gamepad East")
        return "B";
    if (label == "Gamepad West")
        return "X";
    if (label == "Gamepad North")
        return "Y";
    if (label == "Gamepad Back")
        return "BACK";
    if (label == "Gamepad Start")
        return "STRT";
    if (label == "Gamepad Left Stick")
        return "LS";
    if (label == "Gamepad Right Stick")
        return "RS";
    if (label == "Gamepad Left Shoulder")
        return "LB";
    if (label == "Gamepad Right Shoulder")
        return "RB";
    if (label == "Gamepad Left Trigger")
        return "LT";
    if (label == "Gamepad Right Trigger")
        return "RT";
    if (label == "Gamepad D-Pad Up")
        return "DU";
    if (label == "Gamepad D-Pad Down")
        return "DD";
    if (label == "Gamepad D-Pad Left")
        return "DL";
    if (label == "Gamepad D-Pad Right")
        return "DR";
    if (label == "Gamepad Touchpad")
        return "TP";

    constexpr std::string_view gamepadPrefix = "Gamepad ";
    if (label.rfind(gamepadPrefix.data(), 0) == 0)
        label.erase(0, gamepadPrefix.size());

    std::string compact;
    for (const char ch : label) {
        if (std::isalnum(static_cast<unsigned char>(ch)))
            compact.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        if (compact.size() >= 4)
            break;
    }
    return compact.empty() ? "..." : compact;
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
                        float bindingFontSize,
                        float barW,
                        float barH,
                        float uiScale,
                        AbilitySide side,
                        HudIcon abilityIcon,
                        const std::string& bindingLabel,
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

    const float baseFs = bindingFontSize * uiScale;
    const float labelW = ctx.measureText(bindingLabel.c_str(), baseFs);
    const float maxLabelW = bar.w * 0.58f;
    const float fs = labelW > maxLabelW && labelW > 0.f ? std::max(8.f * uiScale, baseFs * (maxLabelW / labelW))
                                                         : baseFs;
    ctx.text(bindingLabel.c_str(),
             bar.x + bar.w * 0.5f,
             bar.y + (bar.h - fs) * 0.5f - fs * 0.18f,
             fs,
             voidfall::k_textBright,
             HudAlign::Center);
}

} // namespace

EquipmentSlots::EquipmentSlots()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -62.f;
    width = iconFrameWidth * 2.f + barWidth * 2.f + iconBarGap * 2.f + centerGap;
    height = std::max(iconFrameHeight, barHeight);

    abilityElements[0].bar.scale = 0.8f;
    abilityElements[0].bar.offsetX = -22.f;
    abilityElements[0].bar.offsetY = -28.f;
    abilityElements[0].bar.stretchX = 1.f;
    abilityElements[0].bar.stretchY = 0.65f;

    abilityElements[1].bar.scale = 0.8f;
    abilityElements[1].bar.offsetX = 22.f;
    abilityElements[1].bar.offsetY = -22.5f;
    abilityElements[1].bar.stretchX = 1.f;
    abilityElements[1].bar.stretchY = 0.65f;
    abilityElements[1].flipBarY = true;
}

void EquipmentSlots::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    state_ = state.equipment;
    visible = state.isAlive;
    if (state.bindings) {
        bindingLabels_[0] = compactBindingLabel(
            InputBindings::bindingLabel(state.bindings->get(Action::Ability1, state.activeInputDevice)));
        bindingLabels_[1] = compactBindingLabel(
            InputBindings::bindingLabel(state.bindings->get(Action::Ability2, state.activeInputDevice)));
    } else {
        bindingLabels_[0] = "";
        bindingLabels_[1] = "";
    }
}

void EquipmentSlots::draw(HudContext& ctx, float anchorX, float anchorY)
{
    const float s = uiScale_;
    const float frameW = iconFrameWidth * s;
    const float frameH = iconFrameHeight * s;
    const float iconSize = abilityIconSize * s;
    const float bindingFs = bindingFontSize;
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
                       bindingFs,
                       barW,
                       barH,
                       s,
                       AbilitySide::Left,
                       iconForAbility(state_.primaryAbilityName, state_.primaryAbilityAvailable),
                       bindingLabels_[0],
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
                       bindingFs,
                       barW,
                       barH,
                       s,
                       AbilitySide::Right,
                       iconForAbility(state_.secondaryAbilityName, state_.secondaryAbilityAvailable),
                       bindingLabels_[1],
                       state_.secondaryAbilityCharge);
}
