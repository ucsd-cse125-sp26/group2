/// @file EquipmentSlots.cpp
/// @brief Voidfall equipment row implementation.

#include "EquipmentSlots.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

EquipmentSlots::EquipmentSlots()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -24.f;
}

void EquipmentSlots::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    state_ = state.equipment;
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

    constexpr int slotCount = 3;
    const float totalW = static_cast<float>(slotCount) * ss + static_cast<float>(slotCount - 1) * gp;
    const float startX = anchorX - totalW * 0.5f;
    const float y = anchorY - ss;

    struct SlotConfig
    {
        const char* keyLabel;
        float charge;
        int count; // -1 = no count (show RDY/secs)
        const char* name;
        bool isGrapple;
        bool isGrenade;
        bool isTactical;
    };

    const SlotConfig slots[3] = {
        {"E", state_.grappleCharge, -1, "GRAPPLE", true, false, false},
        {"G", state_.grenadeCharge, state_.grenadeCount, "GRENADE", false, true, false},
        {"Q", state_.tacticalCharge, state_.tacticalCount, "TACTICAL", false, false, true},
    };

    for (int i = 0; i < slotCount; ++i) {
        const auto& sl = slots[i];
        const float x = startX + static_cast<float>(i) * (ss + gp);
        const bool ready = sl.charge >= 0.999f;
        const HudColor border = ready ? k_amber : k_line;
        const HudColor iconC = ready ? k_amber : k_textDim;

        // Slot background.
        drawPanel(ctx, x, y, ss, ss, k_bgPanel, border, 1.f);

        // Icon (top-left).
        const float ix = x + pad;
        const float iy = y + pad;
        if (sl.isGrapple)
            icons::grapple(ctx, ix, iy, iconSize, iconC);
        else if (sl.isGrenade)
            icons::grenade(ctx, ix, iy, iconSize, iconC);
        else if (sl.isTactical)
            icons::tactical(ctx, ix, iy, iconSize, iconC);

        // Key tab (top-right).
        const float keyFs = keyFontSize * s;
        const float keyW = ctx.measureText(sl.keyLabel, keyFs) + 6.f * s;
        const float keyH = keyFs + 4.f * s;
        const float keyX = x + ss - pad - keyW;
        const float keyY = y + pad - 1.f * s;
        ctx.rect(keyX, keyY, keyW, keyH, HudColor{0.f, 0.f, 0.f, 0.45f});
        ctx.rectOutline(keyX, keyY, keyW, keyH, 1.f, k_lineBright);
        ctx.text(sl.keyLabel, keyX + keyW * 0.5f, keyY + 1.f * s - keyFs * 0.18f, keyFs, k_textDim, HudAlign::Center);

        // Cooldown overlay (covers (1-charge) of slot height from top).
        if (!ready) {
            const float overH = ss * (1.f - std::clamp(sl.charge, 0.f, 1.f));
            ctx.rect(x, y, ss, overH, HudColor{0.f, 0.f, 0.f, 0.55f});
        }

        // Status text bottom-left.
        const float statusFs = statusFontSize * s;
        const float countFs = countFontSize * s;
        const float statY = y + ss - pad - statusFs;
        if (sl.count >= 0) {
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
    }
}
