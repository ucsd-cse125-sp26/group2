/// @file KillFeed.cpp
/// @brief Voidfall kill-feed implementation.

#include "KillFeed.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <cstring>

namespace
{

/// @brief Map weapon ID to a four-letter mil-spec callsign for the kill feed.
const char* weaponCallsign(int /*weaponId*/)
{
    // The HudKillFeedEntry currently only carries a numeric weaponId; we don't
    // have a typed map yet.  Default to "ARC-9" until the feed payload grows
    // to include weapon names — same fallback as the prototype.
    return "ARC-9";
}

bool nameMatchesYou(const std::string& s)
{
    return s == "You" || s == "YOU";
}

} // namespace

KillFeed::KillFeed()
{
    anchor = HudAnchor::TopRight;
    offsetX = -80.f;
    offsetY = 28.f;
}

void KillFeed::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    for (const auto& ev : state.killFeedEvents) {
        Entry e;
        e.killerName = ev.killerName;
        e.victimName = ev.victimName;
        e.isHeadshot = ev.isHeadshot;
        e.youAreKiller = nameMatchesYou(ev.killerName);
        e.youAreVictim = nameMatchesYou(ev.victimName);
        e.timer = entryLifetime;
        e.slideIn = 0.f;
        entries_.insert(entries_.begin(), e);
    }

    for (auto& e : entries_) {
        if (e.permanent)
            continue;
        e.timer -= dt;
        e.slideIn = std::min(1.f, e.slideIn + dt * 6.f); // ~0.16s slide-in
    }
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) {
                       return !e.permanent && e.timer <= 0.f;
                   }),
                   entries_.end());

    while (static_cast<int>(entries_.size()) > maxEntries) {
        const auto removable =
            std::find_if(entries_.rbegin(), entries_.rend(), [](const Entry& e) { return !e.permanent; });
        if (removable == entries_.rend())
            break;
        entries_.erase(std::next(removable).base());
    }
}

void KillFeed::draw(HudContext& ctx, float anchorX, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float fs = fontSize * s;
    const float eh = entryHeight * s;
    const float ep = entryPadding * s;
    const float padX = 8.f * s;
    const float gap = 7.f * s;

    float curY = y;
    for (const auto& e : entries_) {
        const float alpha = (e.permanent ? 1.f : std::min(e.timer / fadeOutDuration, 1.f)) * e.slideIn;
        const float slideOff = (1.f - e.slideIn) * 16.f * s;

        const char* weapon = weaponCallsign(0);
        const float killerW = ctx.measureText(e.killerName.c_str(), fs);
        const float weaponW = ctx.measureText(weapon, fs);
        const float victimW = ctx.measureText(e.victimName.c_str(), fs);
        const float hsW = e.isHeadshot ? (10.f * s + gap) : 0.f;
        const float contentW = killerW + gap + weaponW + hsW + gap + victimW;
        const float pillW = contentW + padX * 2.f;
        const float pillH = eh;

        const float pillX = anchorX - pillW + slideOff;
        const float pillY = curY;

        // Background — amber-tinted when the local player is involved.
        const HudColor bg =
            e.youAreKiller ? withAlpha(k_secondary, 0.65f * alpha) : withAlpha(k_quaternary, 0.78f * alpha);
        const HudColor border = e.youAreKiller ? withAlpha(k_amber, alpha) : withAlpha(k_lineDim, alpha);
        ctx.rect(pillX, pillY, pillW, pillH, bg);
        ctx.rectOutline(pillX, pillY, pillW, pillH, 1.f, border);

        // Killer name (amber-tinted if local player).
        const float textY = pillY + (pillH - fs) * 0.5f - fs * 0.18f;
        float cursorX = pillX + padX;
        const HudColor killerColor = e.youAreKiller ? withAlpha(k_textBright, alpha) : withAlpha(k_text, alpha);
        ctx.text(e.killerName.c_str(), cursorX, textY, fs, killerColor, HudAlign::Left);
        cursorX += killerW + gap;

        // Weapon abbreviation in amber.
        ctx.text(weapon, cursorX, textY, fs, withAlpha(k_amber, alpha), HudAlign::Left);
        cursorX += weaponW + gap;

        // Headshot glyph: small red diamond.
        if (e.isHeadshot) {
            const float hsSize = 8.f * s;
            ctx.rotatedRect(
                cursorX + hsSize * 0.5f, pillY + pillH * 0.5f, hsSize, hsSize, 45.f, withAlpha(k_red, alpha));
            cursorX += 10.f * s + gap;
        }

        // Victim name in red.
        const HudColor victimColor = e.youAreVictim ? withAlpha(k_red, alpha) : withAlpha(k_textDim, alpha);
        ctx.text(e.victimName.c_str(), cursorX, textY, fs, victimColor, HudAlign::Left);

        curY += pillH + ep;
    }
}
