/// @file PickupNotification.cpp
/// @brief Voidfall pickup-notification implementation.

#include "PickupNotification.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

PickupNotification::PickupNotification()
{
    anchor = HudAnchor::TopRight;
    offsetX = -20.f;
    offsetY = 240.f;
}

void PickupNotification::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    for (const auto& n : state.pickupNotifications) {
        Entry e;
        e.label = n.label;
        e.qty = n.qty;
        e.timer = entryLifetime;
        e.slideIn = 0.f;
        entries_.insert(entries_.begin(), e);
    }

    for (auto& e : entries_) {
        e.timer -= dt;
        e.slideIn = std::min(1.f, e.slideIn + dt * 5.f);
    }
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.timer <= 0.f; }),
                   entries_.end());
}

void PickupNotification::draw(HudContext& ctx, float anchorX, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float fs = entryFontSize * s;
    const float eh = entryHeight * s;
    const float gap = entryGap * s;
    const float padX = 10.f * s;

    float curY = y;
    for (const auto& e : entries_) {
        const float alpha = std::min(e.timer / fadeOut, 1.f) * e.slideIn;
        const float slideOff = (1.f - e.slideIn) * 12.f * s;

        char qtyBuf[8];
        SDL_snprintf(qtyBuf, sizeof(qtyBuf), "+%d", e.qty);
        const float qtyW = ctx.measureText(qtyBuf, fs);
        const float labelW = ctx.measureText(e.label.c_str(), fs);
        const float totalW = qtyW + 10.f * s + labelW + padX * 2.f;

        const float pillX = anchorX - totalW + slideOff;
        const float pillY = curY;

        ctx.rect(pillX, pillY, totalW, eh, withAlpha(k_pickupBg, alpha));
        ctx.rectOutline(pillX, pillY, totalW, eh, 1.f, withAlpha(k_line, alpha));
        // Amber left edge accent.
        ctx.rect(pillX, pillY, 2.f * s, eh, withAlpha(k_amber, alpha));

        const float textY = pillY + (eh - fs) * 0.5f - fs * 0.18f;
        ctx.text(qtyBuf, pillX + padX + 2.f * s, textY, fs, withAlpha(k_amber, alpha), HudAlign::Left);
        ctx.text(e.label.c_str(),
                 pillX + padX + 2.f * s + qtyW + 10.f * s,
                 textY,
                 fs,
                 withAlpha(k_textBright, alpha),
                 HudAlign::Left);

        curY += eh + gap;
    }
}
