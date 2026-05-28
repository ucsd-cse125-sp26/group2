/// @file EnemyWorldHealthBar.cpp
/// @brief Voidfall floating enemy HP bar implementation.

#include "EnemyWorldHealthBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

EnemyWorldHealthBar::EnemyWorldHealthBar()
{
    anchor = HudAnchor::TopLeft;
    visible = true;
}

void EnemyWorldHealthBar::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    viewProj_ = state.viewProj;
    screenW_ = state.screenW;
    screenH_ = state.screenH;

    // Mark every cached entry stale, then refresh those still in the world.
    for (auto& [_, e] : enemies_)
        e.alive = false;

    for (const auto& we : state.worldEnemies) {
        EnemyState& e = enemies_[we.name];
        e.worldX = we.worldX;
        e.worldY = we.worldY;
        e.worldZ = we.worldZ;
        e.name = we.name;
        e.maxHp = std::max(1, we.maxHealth);
        e.maxSh = std::max(1, we.maxArmor);
        e.alive = we.isAlive;

        const int newHp = we.health;
        const int newSh = we.armor;

        // First time we've seen this enemy — seed the cache silently, no
        // visibility window.  Otherwise: refresh the show timer whenever the
        // enemy's HP or shield drops, so the bar pops into view exactly when
        // they're being shot at and lingers for `showAfterDamageSecs` after
        // the last hit before fading.
        const bool tookDamage = e.initialized && (newHp < e.displayHp || newSh < e.displaySh);
        if (tookDamage)
            e.showTimer = showAfterDamageSecs + fadeOutSecs;
        e.initialized = true;

        if (newHp < e.displayHp) {
            e.trailHp = static_cast<float>(e.displayHp) / static_cast<float>(e.maxHp);
            e.trailHpHold = 0.4f;
        } else if (newHp > e.displayHp) {
            e.trailHp = static_cast<float>(newHp) / static_cast<float>(e.maxHp);
        }
        if (newSh < e.displaySh) {
            e.trailSh = static_cast<float>(e.displaySh) / static_cast<float>(e.maxSh);
            e.trailShHold = 0.4f;
        } else if (newSh > e.displaySh) {
            e.trailSh = static_cast<float>(newSh) / static_cast<float>(e.maxSh);
        }

        e.hp = newHp;
        e.sh = newSh;
        e.displayHp = newHp;
        e.displaySh = newSh;

        const float targetHp = static_cast<float>(e.hp) / static_cast<float>(e.maxHp);
        const float targetSh = static_cast<float>(e.sh) / static_cast<float>(e.maxSh);
        const float lerp = std::clamp(dt * 12.f, 0.f, 1.f);
        e.liveHp += (targetHp - e.liveHp) * lerp;
        e.liveSh += (targetSh - e.liveSh) * lerp;
    }

    // Decay trail holds, drain trails, decay show timer.
    for (auto& [_, e] : enemies_) {
        if (e.trailHpHold > 0.f)
            e.trailHpHold = std::max(0.f, e.trailHpHold - dt);
        if (e.trailShHold > 0.f)
            e.trailShHold = std::max(0.f, e.trailShHold - dt);
        if (e.trailHpHold <= 0.f && e.trailHp > e.liveHp)
            e.trailHp = std::max(e.liveHp, e.trailHp - dt / 0.6f);
        if (e.trailShHold <= 0.f && e.trailSh > e.liveSh)
            e.trailSh = std::max(e.liveSh, e.trailSh - dt / 0.6f);

        if (e.showTimer > 0.f)
            e.showTimer = std::max(0.f, e.showTimer - dt);
    }

    // Garbage-collect stale entries (enemy left the world).
    for (auto it = enemies_.begin(); it != enemies_.end();) {
        if (!it->second.alive)
            it = enemies_.erase(it);
        else
            ++it;
    }
}

void EnemyWorldHealthBar::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float bw = barWidth * s;
    const float shH = shieldHeight * s;
    const float hpH = healthHeight * s;
    const float fs = fontSize * s;

    for (const auto& [_, e] : enemies_) {
        if (!e.alive)
            continue;
        // Hide entirely until this enemy has been damaged recently.
        if (e.showTimer <= 0.f)
            continue;

        // Project world point to clip space.
        const glm::vec4 clip = viewProj_ * glm::vec4(e.worldX, e.worldY, e.worldZ, 1.f);
        if (clip.w <= 0.f)
            continue; // behind camera
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        if (std::abs(ndcX) > 1.2f || std::abs(ndcY) > 1.2f)
            continue;
        float sx = std::round((ndcX * 0.5f + 0.5f) * screenW_);
        // NDC Y grows up, screen Y grows down — flip the Y axis. Without this
        // flip, looking up makes the bar drift downward and vice versa.
        float sy = std::round((1.0f - (ndcY * 0.5f + 0.5f)) * screenH_);
        sy -= yOffsetPx * s;

        // Fade alpha during the last `fadeOutSecs` of the visibility window.
        const float alpha = std::clamp(e.showTimer / fadeOutSecs, 0.f, 1.f);

        // Layout (top-down): [name]  [shield bar]  [hp bar].  No HP number,
        // shorter bars, name pixel-snapped so it doesn't shimmer.
        const float barX = std::round(sx - bw * 0.5f);

        // Name baseline above the bars — design feedback wanted the
        // nickname *in front* of the bars, not behind.
        const float nameTop = std::round(sy - hpH - (e.maxSh > 0 ? shH + 2.f * s : 0.f) - 4.f * s - fs);
        // Name floats over the world → outline-on for legibility against
        // bright sky / textured environments.
        ctx.text(e.name.c_str(), sx, nameTop, fs, withAlpha(k_health, alpha), HudAlign::Center, /*outlined=*/true);

        // Shield bar (above HP) — only when target has any shield max.
        // Gradient cyanDim → cyan to match player vitals + design CSS.
        float barY = std::round(sy - hpH);
        if (e.maxSh > 0 && (e.liveSh > 0.001f || e.trailSh > 0.001f)) {
            const float bgY = std::round(barY - shH - 2.f * s);
            drawGradientTrailBar(ctx,
                                 barX,
                                 bgY,
                                 bw,
                                 shH,
                                 std::clamp(e.liveSh, 0.f, 1.f),
                                 std::clamp(e.trailSh, 0.f, 1.f),
                                 withAlpha(k_cyanDim, alpha),
                                 withAlpha(k_cyan, alpha),
                                 withAlpha(k_tertiary, 0.45f * alpha),
                                 withAlpha(k_quaternary, 0.70f * alpha),
                                 withAlpha(k_lineDim, alpha));
        }

        // HP bar — gradient red → red-bright (same as player vitals).
        drawGradientTrailBar(ctx,
                             barX,
                             barY,
                             bw,
                             hpH,
                             std::clamp(e.liveHp, 0.f, 1.f),
                             std::clamp(e.trailHp, 0.f, 1.f),
                             withAlpha(k_health, alpha),
                             withAlpha(k_healthBright, alpha),
                             withAlpha(k_tertiary, 0.45f * alpha),
                             withAlpha(k_quaternary, 0.70f * alpha),
                             withAlpha(k_lineDim, alpha));
    }
}
