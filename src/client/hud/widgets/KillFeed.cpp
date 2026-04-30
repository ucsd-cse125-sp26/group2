/// @file KillFeed.cpp
#include "KillFeed.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>

KillFeed::KillFeed()
{
    anchor = HudAnchor::TopRight;
    offsetX = -10.f;
    offsetY = 10.f;
}

void KillFeed::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    // Ingest new events.
    for (const auto& ev : state.killFeedEvents) {
        Entry e;
        e.killerName = ev.killerName;
        e.victimName = ev.victimName;
        e.isHeadshot = ev.isHeadshot;
        e.timer = entryLifetime;
        entries_.insert(entries_.begin(), e);
    }

    // Tick timers and remove expired.
    for (auto& e : entries_)
        e.timer -= dt;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.timer <= 0.f; }),
                   entries_.end());

    // Cap entry count.
    if (static_cast<int>(entries_.size()) > maxEntries)
        entries_.resize(static_cast<std::size_t>(maxEntries));
}

void KillFeed::draw(HudContext& ctx, float x, float y)
{
    const float s = uiScale_;
    const float fs = fontSize * s;
    const float eh = entryHeight * s;
    const float ep = entryPadding * s;

    float curY = y;
    for (const auto& e : entries_) {
        // Fade out in the last fadeOutDuration seconds.
        const float alpha = std::min(e.timer / fadeOutDuration, 1.f);
        const HudColor killerColor(1.f, 1.f, 1.f, alpha);
        const HudColor victimColor(0.8f, 0.2f, 0.2f, alpha);

        // "Killer > Victim" (right-aligned from anchor).
        const char* arrow = " > ";
        const float killerW = ctx.measureText(e.killerName.c_str(), fs);
        const float arrowW = ctx.measureText(arrow, fs);
        const float victimW = ctx.measureText(e.victimName.c_str(), fs);
        const float totalW = killerW + arrowW + victimW;

        float curX = x - totalW;
        ctx.text(e.killerName.c_str(), curX, curY, fs, killerColor);
        curX += killerW;
        ctx.text(arrow, curX, curY, fs, HudColor(0.7f, 0.7f, 0.7f, alpha));
        curX += arrowW;
        ctx.text(e.victimName.c_str(), curX, curY, fs, victimColor);

        curY += eh + ep;
    }
}
