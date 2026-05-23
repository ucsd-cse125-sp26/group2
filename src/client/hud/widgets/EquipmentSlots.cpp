/// @file EquipmentSlots.cpp
/// @brief Voidfall ability row implementation.

#include "EquipmentSlots.hpp"

#include "config/InputBindings.hpp"
#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

EquipmentSlots::EquipmentSlots()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -60.f;
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
    const float pad = 6.f * s;
    const float iconSize = 24.f * s;

    constexpr int slotCount = 2;
    const float totalW = static_cast<float>(slotCount) * ss + static_cast<float>(slotCount - 1) * gp;
    const float startX = anchorX - totalW * 0.5f;
    const float y = anchorY - ss;

    struct SlotConfig
    {
        const std::string* keyLabel;
        float charge;
        int count; // -1 = no count (show RDY/secs)
        std::string name;
        bool isGrapple;
        bool isTactical;
        bool available;
        bool marked;
    };

    const SlotConfig slots[slotCount] = {
        {&primaryAbilityLabel_,
         state_.primaryAbilityCharge,
         -1,
         state_.primaryAbilityAvailable ? state_.primaryAbilityName : "LOCKED",
         state_.primaryAbilityName == "GRAPPLE",
         state_.primaryAbilityName != "GRAPPLE",
         state_.primaryAbilityAvailable,
         false},
        {&secondaryAbilityLabel_,
         state_.secondaryAbilityCharge,
         -1,
         state_.secondaryAbilityAvailable ? state_.secondaryAbilityName : "LOCKED",
         false,
         true,
         state_.secondaryAbilityAvailable,
         state_.secondaryAbilityMarked},
    };

    for (int i = 0; i < slotCount; ++i) {
        const auto& sl = slots[i];
        const float x = startX + static_cast<float>(i) * (ss + gp);
        const bool ready = sl.available && sl.charge >= 0.999f;
        const HudColor border = ready ? k_amber : (sl.available ? k_line : k_lineDim);
        const HudColor iconC = ready ? k_amber : (sl.available ? k_textDim : withAlpha(k_textDim, 0.45f));

        // Slot background.
        drawPanel(ctx, x, y, ss, ss, k_bgPanel, border, 1.f);

        // Icon (top-left).
        const float ix = x + pad;
        const float iy = y + pad;
        if (sl.isGrapple)
            icons::grapple(ctx, ix, iy, iconSize, iconC);
        else if (sl.isTactical)
            icons::tactical(ctx, ix, iy, iconSize, iconC);

        // Key tab (top-right).
        const char* keyLabel = sl.keyLabel->c_str();
        const float maxKeyW = ss - pad * 2.f;
        float keyFs = keyFontSize * s;
        while (keyFs > 5.0f * s && ctx.measureText(keyLabel, keyFs) + 6.f * s > maxKeyW) {
            keyFs -= 0.5f * s;
        }
        const float keyW = std::min(maxKeyW, ctx.measureText(keyLabel, keyFs) + 6.f * s);
        const float keyH = keyFs + 4.f * s;
        const float keyX = x + ss - pad - keyW;
        const float keyY = y + pad - 1.f * s;
        ctx.rect(keyX, keyY, keyW, keyH, HudColor{0.f, 0.f, 0.f, 0.45f});
        ctx.rectOutline(keyX, keyY, keyW, keyH, 1.f, k_lineBright);
        ctx.text(keyLabel, keyX + keyW * 0.5f, keyY + 1.f * s - keyFs * 0.18f, keyFs, k_textDim, HudAlign::Center);

        // Cooldown overlay (covers (1-charge) of slot height from top).
        if (sl.available && !ready) {
            const float overH = ss * (1.f - std::clamp(sl.charge, 0.f, 1.f));
            ctx.rect(x, y, ss, overH, HudColor{0.f, 0.f, 0.f, 0.55f});
        } else if (!sl.available) {
            ctx.rect(x, y, ss, ss, HudColor{0.f, 0.f, 0.f, 0.45f});
        }

        // Status text bottom-left.
        const float statusFs = statusFontSize * s;
        const float countFs = countFontSize * s;
        const float statY = y + ss - pad - statusFs;
        if (!sl.available) {
            ctx.text("LOCK", x + pad, statY, statusFs, k_textDim, HudAlign::Left);
        } else if (sl.marked) {
            ctx.text("MARK", x + pad, statY, statusFs, k_cyan, HudAlign::Left);
        } else if (sl.count >= 0) {
            char buf[8];
            SDL_snprintf(buf, sizeof(buf), "%d", sl.count);
            ctx.text(buf, x + pad, y + ss - pad - countFs, countFs, ready ? k_amber : k_textDim, HudAlign::Left);
        } else if (ready) {
            ctx.text("RDY", x + pad, statY, statusFs, k_amber, HudAlign::Left);
        } else {
            const int secs = static_cast<int>(std::ceil((1.f - sl.charge) * 8.f));
            char buf[8];
            SDL_snprintf(buf, sizeof(buf), "%ds", secs);
            ctx.text(buf, x + pad, statY, statusFs, k_textDim, HudAlign::Left);
        }

        const float nameFs = nameFontSize * s;
        ctx.text(
            sl.name.c_str(), x + pad, y + ss + 4.f * s, nameFs, sl.available ? k_textDim : withAlpha(k_textDim, 0.55f));
    }
}
