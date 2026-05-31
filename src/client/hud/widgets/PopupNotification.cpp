/// @file PopupNotification.cpp
/// @brief Bottom-center transient HUD popup messages.

#include "PopupNotification.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

namespace
{

/// @brief Pick the left-edge accent color for a popup category.
HudColor accentFor(HudPopupKind kind)
{
    using namespace voidfall;

    switch (kind) {
    case HudPopupKind::Success:
    case HudPopupKind::PlayerJoined:
        return k_green;
    case HudPopupKind::Warning:
    case HudPopupKind::PlayerLeft:
        return k_amber;
    case HudPopupKind::Info:
    default:
        return k_cyan;
    }
}

} // namespace

PopupNotification::PopupNotification()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -168.f;
}

void PopupNotification::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    // Incoming popup messages are one-frame events. Retained entries below own
    // their lifetime so producers do not need to resend the same message.
    for (const auto& popup : state.popupMessages) {
        Entry entry;
        entry.kind = popup.kind;
        entry.text = popup.text;
        entry.timer = entryLifetime;
        entry.slideIn = 0.f;
        entries_.insert(entries_.begin(), std::move(entry));
    }

    for (auto& entry : entries_) {
        entry.timer -= dt;
        entry.slideIn = std::min(1.f, entry.slideIn + dt * 6.f);
    }

    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [](const Entry& entry) { return entry.timer <= 0.f; }),
        entries_.end());

    while (static_cast<int>(entries_.size()) > maxEntries) {
        entries_.pop_back();
    }
}

void PopupNotification::draw(HudContext& ctx, float centerX, float bottomY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float fs = entryFontSize * s;
    const float eh = entryHeight * s;
    const float gap = entryGap * s;
    const float padX = 14.f * s;
    const float minW = 260.f * s;

    float curY = bottomY - eh;
    for (const auto& entry : entries_) {
        const float alpha = std::min(entry.timer / fadeOut, 1.f) * entry.slideIn;
        const float slideOff = (1.f - entry.slideIn) * 16.f * s;
        const float textW = ctx.measureText(entry.text.c_str(), fs);
        const float pillW = std::max(minW, textW + padX * 2.f);
        const float pillX = centerX - pillW * 0.5f;
        const float pillY = curY + slideOff;
        const HudColor accent = accentFor(entry.kind);

        ctx.rect(pillX, pillY, pillW, eh, withAlpha(k_bgPanelSolid, 0.78f * alpha));
        ctx.rectOutline(pillX, pillY, pillW, eh, 1.f, withAlpha(k_line, alpha));
        ctx.rect(pillX, pillY, 3.f * s, eh, withAlpha(accent, alpha));

        const float textY = pillY + (eh - fs) * 0.5f - fs * 0.18f;
        ctx.text(entry.text.c_str(), centerX, textY, fs, withAlpha(k_textBright, alpha), HudAlign::Center);

        curY -= eh + gap;
    }
}
