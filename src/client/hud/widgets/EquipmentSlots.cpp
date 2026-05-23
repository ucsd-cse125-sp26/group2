/// @file EquipmentSlots.cpp
/// @brief Voidfall ability row implementation.

#include "EquipmentSlots.hpp"

#include "config/InputBindings.hpp"
#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <string>

EquipmentSlots::EquipmentSlots()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -110.f;
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
    const float ss = slotSize * s;
    const float gp = slotGap * s;
    const float icon = iconSize * s;
    const float keyPadW = keyPadX * s;
    const float keyPadH = keyPadY * s;

    constexpr int slotCount = 2;
    const float totalW = static_cast<float>(slotCount) * ss + static_cast<float>(slotCount - 1) * gp;
    const float startX = anchorX - totalW * 0.5f;
    const float y = anchorY - ss;

    struct SlotConfig
    {
        const std::string* keyLabel;
        float charge;
        bool isGrapple;
        bool isTactical;
        bool available;
    };

    const SlotConfig slots[slotCount] = {
        {&primaryAbilityLabel_,
         state_.primaryAbilityCharge,
         state_.primaryAbilityName == "GRAPPLE",
         state_.primaryAbilityName != "GRAPPLE",
         state_.primaryAbilityAvailable},
        {&secondaryAbilityLabel_,
         state_.secondaryAbilityCharge,
         false,
         true,
         state_.secondaryAbilityAvailable},
    };

    for (int i = 0; i < slotCount; ++i) {
        const auto& sl = slots[i];
        const float x = startX + static_cast<float>(i) * (ss + gp);
        const bool ready = sl.available && sl.charge >= 0.999f;
        const HudColor iconC = ready ? k_amber : (sl.available ? k_textDim : withAlpha(k_textDim, 0.45f));

        const float ix = x + (ss - icon) * 0.5f;
        const float iy = y;
        if (sl.isGrapple)
            icons::grapple(ctx, ix, iy, icon, iconC);
        else if (sl.isTactical)
            icons::tactical(ctx, ix, iy, icon, iconC);

        const char* keyLabel = sl.keyLabel->c_str();
        float keyFs = keyFontSize * s;
        const float maxKeyW = ss;
        while (keyFs > 5.0f * s && ctx.measureText(keyLabel, keyFs) + keyPadW * 2.f > maxKeyW) {
            keyFs -= 0.5f * s;
        }
        const float keyW = std::min(maxKeyW, ctx.measureText(keyLabel, keyFs) + keyPadW * 2.f);
        const float keyH = keyFs + keyPadH * 2.f;
        const float keyX = x + (ss - keyW) * 0.5f;
        const float keyY = y + icon + 7.f * s;
        const HudColor keyColor = ready ? k_amber : (sl.available ? k_textDim : withAlpha(k_textDim, 0.45f));
        const HudColor cutoutColor{0.03f, 0.035f, 0.04f, sl.available ? 0.92f : 0.62f};
        ctx.rect(keyX, keyY, keyW, keyH, keyColor);
        ctx.text(
            keyLabel, keyX + keyW * 0.5f, keyY + keyPadH - keyFs * 0.18f, keyFs, cutoutColor, HudAlign::Center);
    }
}
