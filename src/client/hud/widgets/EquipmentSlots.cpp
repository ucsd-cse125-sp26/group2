/// @file EquipmentSlots.cpp
/// @brief Voidfall ability row implementation.

#include "EquipmentSlots.hpp"

#include "config/InputBindings.hpp"
#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <string>

namespace
{
void drawCutCornerOutline(HudContext& ctx, float x, float y, float w, float h, float cut, float thickness, HudColor color)
{
    const float c = std::clamp(cut, 0.f, std::min(w, h) * 0.32f);
    const float points[] = {
        x + c,
        y,
        x + w - c,
        y,
        x + w,
        y + c,
        x + w,
        y + h - c,
        x + w - c,
        y + h,
        x + c,
        y + h,
        x,
        y + h - c,
        x,
        y + c,
        x + c,
        y,
    };
    ctx.polyline(points, 9, thickness, color);
}

void drawSegmentedChargeBar(HudContext& ctx,
                            float x,
                            float y,
                            float w,
                            float h,
                            float fill01,
                            int segments,
                            HudColor leftColor,
                            HudColor rightColor)
{
    using namespace voidfall;

    const float fill = std::clamp(fill01, 0.f, 1.f);
    drawPanel(ctx, x, y, w, h, withAlpha(k_bgInset, 0.72f), withAlpha(k_lineBright, 0.50f), 1.f);

    const float pad = std::max(1.f, h * 0.18f);
    const float gap = std::max(1.f, h * 0.16f);
    const float innerX = x + pad;
    const float innerY = y + pad;
    const float innerW = w - pad * 2.f;
    const float innerH = h - pad * 2.f;
    const float segmentW = (innerW - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        const float segStart01 = static_cast<float>(i) / static_cast<float>(segments);
        const float segEnd01 = static_cast<float>(i + 1) / static_cast<float>(segments);
        const float localFill = std::clamp((fill - segStart01) / (segEnd01 - segStart01), 0.f, 1.f);
        const float sx = innerX + static_cast<float>(i) * (segmentW + gap);

        ctx.rect(sx, innerY, segmentW, innerH, withAlpha(k_quaternary, 0.42f));
        if (localFill > 0.f) {
            const HudColor segLeft = lerpColor(leftColor, rightColor, segStart01);
            const HudColor segRight = lerpColor(leftColor, rightColor, segEnd01);
            ctx.gradientRect(sx, innerY, segmentW * localFill, innerH, segLeft, segRight);
            ctx.rect(sx + segmentW * localFill - std::max(1.f, innerH * 0.10f),
                     innerY,
                     std::max(1.f, innerH * 0.10f),
                     innerH,
                     withAlpha(k_textBright, 0.65f));
        }
    }
}
} // namespace

EquipmentSlots::EquipmentSlots()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -40.f;
    width = slotSize * 2.f + slotGap * 2.f + chargeBarWidth;
    height = slotHeight + keyFontSize + keyPadY * 2.f + 8.f;
}

void EquipmentSlots::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    state_ = state.equipment;
    if (state.bindings) {
        primaryAbilityLabel_ = InputBindings::bindingLabel(state.bindings->get(Action::Ability1));
        secondaryAbilityLabel_ = InputBindings::bindingLabel(state.bindings->get(Action::Ability2));
    }
    visible = state.isAlive;
}

// Icon shapes are now centralised in `hud/HudIcons` so every widget reaches
// the same shield/grapple/grenade silhouettes.

void EquipmentSlots::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float icon = iconSize * s;
    const float frameW = slotSize * s;
    const float frameH = slotHeight * s;
    const float barW = chargeBarWidth * s;
    const float barH = chargeBarHeight * s;
    const float gap = slotGap * s;
    const float totalW = frameW * 2.f + barW + gap * 2.f;
    const float x = anchorX - totalW * 0.5f;
    const float y = anchorY - frameH;
    const float barsX = x + frameW + gap;
    const float rightX = barsX + barW + gap;

    constexpr int slotCount = 2;
    struct SlotConfig
    {
        const std::string* keyLabel;
        float charge;
        bool available;
        HudColor accent;
    };

    const SlotConfig slots[slotCount] = {
        {&primaryAbilityLabel_,
         state_.primaryAbilityCharge,
         state_.primaryAbilityAvailable,
         k_purpleBright},
        {&secondaryAbilityLabel_,
         state_.secondaryAbilityCharge,
         state_.secondaryAbilityAvailable,
         state_.secondaryAbilityMarked ? k_yellow : k_cyan},
    };

    drawPanel(ctx,
              x - 20.f * s,
              y + frameH * 0.46f,
              totalW + 40.f * s,
              frameH * 0.70f,
              withAlpha(k_bgPanel, 0.28f),
              withAlpha(k_lineBright, 0.38f),
              std::max(1.f, s));

    drawSegmentedChargeBar(ctx,
                           barsX,
                           y + 11.f * s,
                           barW * 0.52f,
                           barH,
                           slots[0].available ? slots[0].charge : 0.f,
                           8,
                           k_purple,
                           k_purpleBright);
    drawSegmentedChargeBar(ctx,
                           barsX + barW * 0.57f,
                           y + 11.f * s,
                           barW * 0.34f,
                           barH,
                           slots[1].available ? slots[1].charge : 0.f,
                           4,
                           k_amberDeep,
                           k_yellow);
    drawSegmentedChargeBar(ctx,
                           barsX,
                           y + 58.f * s,
                           barW * 0.45f,
                           barH,
                           0.f,
                           1,
                           k_health,
                           k_healthBright);
    drawSegmentedChargeBar(ctx,
                           barsX + barW * 0.57f,
                           y + 58.f * s,
                           barW * 0.38f,
                           barH,
                           0.f,
                           1,
                           k_cyanDim,
                           k_cyan);

    for (int i = 0; i < slotCount; ++i) {
        const auto& sl = slots[i];
        const float sx = i == 0 ? x : rightX;
        const bool ready = sl.available && sl.charge >= 0.999f;
        const HudColor accent = ready ? sl.accent : (sl.available ? withAlpha(sl.accent, 0.58f) : withAlpha(k_textDim, 0.36f));

        drawPanel(ctx,
                  sx,
                  y,
                  frameW,
                  frameH,
                  withAlpha(k_bgPanelSolid, ready ? 0.52f : 0.34f),
                  ready ? accent : withAlpha(k_lineBright, 0.56f),
                  std::max(1.f, 1.25f * s));
        drawCutCornerOutline(ctx, sx, y, frameW, frameH, 17.f * s, std::max(1.f, 1.5f * s), withAlpha(accent, 0.86f));

        const float ix = sx + (frameW - icon) * 0.5f;
        const float iy = y + 14.f * s;
        ctx.icon(HudIcon::NoIcon, ix, iy, icon, accent);

        const char* keyLabel = sl.keyLabel->c_str();
        float keyFs = keyFontSize * s;
        const float maxKeyW = frameW;
        while (keyFs > 5.0f * s && ctx.measureText(keyLabel, keyFs) + keyPadX * s * 2.f > maxKeyW) {
            keyFs -= 0.5f * s;
        }
        const float keyW = std::min(maxKeyW, ctx.measureText(keyLabel, keyFs) + keyPadX * s * 2.f);
        const float keyX = sx + (frameW - keyW) * 0.5f;
        const float keyY = y + frameH + 5.f * s;
        drawKeyTab(ctx,
                   keyLabel,
                   keyX,
                   keyY,
                   keyFs,
                   keyPadX * s,
                   keyPadY * s,
                   withAlpha(k_bgPanelSolid, 0.72f),
                   withAlpha(accent, 0.82f),
                   k_textBright);
    }
}
