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

    // Decay trail holds, then drain trails.
    for (auto& [_, e] : enemies_) {
        if (e.trailHpHold > 0.f)
            e.trailHpHold = std::max(0.f, e.trailHpHold - dt);
        if (e.trailShHold > 0.f)
            e.trailShHold = std::max(0.f, e.trailShHold - dt);
        if (e.trailHpHold <= 0.f && e.trailHp > e.liveHp)
            e.trailHp = std::max(e.liveHp, e.trailHp - dt / 0.6f);
        if (e.trailShHold <= 0.f && e.trailSh > e.liveSh)
            e.trailSh = std::max(e.liveSh, e.trailSh - dt / 0.6f);
    }

    // Garbage-collect stale entries (enemy left the visible set & trails finished).
    for (auto it = enemies_.begin(); it != enemies_.end();) {
        if (!it->second.alive && it->second.trailHpHold <= 0.f && it->second.trailShHold <= 0.f &&
            it->second.trailHp <= it->second.liveHp + 0.001f && it->second.trailSh <= it->second.liveSh + 0.001f)
        {
            it = enemies_.erase(it);
        } else {
            ++it;
        }
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

        // Project world point to clip space.
        const glm::vec4 clip = viewProj_ * glm::vec4(e.worldX, e.worldY, e.worldZ, 1.f);
        if (clip.w <= 0.f)
            continue;
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        if (std::abs(ndcX) > 1.2f || std::abs(ndcY) > 1.2f)
            continue;
        float sx = (ndcX * 0.5f + 0.5f) * screenW_;
        float sy = (ndcY * 0.5f + 0.5f) * screenH_;
        sy -= yOffsetPx * s;

        // Header: name + raw HP value.
        char hpText[16];
        SDL_snprintf(hpText, sizeof(hpText), "%d", e.hp);
        const float nameW = ctx.measureText(e.name.c_str(), fs);
        const float numW = ctx.measureText(hpText, fs * 0.85f);
        const float headerW = nameW + numW + 8.f * s;
        const float headerY = sy - fs - 6.f * s;

        ctx.text(e.name.c_str(), sx - headerW * 0.5f, headerY, fs, k_red, HudAlign::Left);
        ctx.text(hpText, sx + headerW * 0.5f, headerY + fs * 0.15f, fs * 0.85f, k_textDim, HudAlign::Right);

        // Shield bar (above HP) — only when target has any shield max.
        const float barX = sx - bw * 0.5f;
        float barY = sy - hpH;
        if (e.maxSh > 0 && (e.liveSh > 0.001f || e.trailSh > 0.001f)) {
            const float bgY = barY - shH - 2.f * s;
            drawTrailBar(ctx,
                         barX,
                         bgY,
                         bw,
                         shH,
                         std::clamp(e.liveSh, 0.f, 1.f),
                         std::clamp(e.trailSh, 0.f, 1.f),
                         k_cyan,
                         HudColor{0.95f, 0.95f, 0.95f, 0.45f},
                         HudColor{0.04f, 0.04f, 0.04f, 0.7f},
                         k_lineDim);
        }

        // HP bar.
        drawTrailBar(ctx,
                     barX,
                     barY,
                     bw,
                     hpH,
                     std::clamp(e.liveHp, 0.f, 1.f),
                     std::clamp(e.trailHp, 0.f, 1.f),
                     k_red,
                     HudColor{0.95f, 0.95f, 0.95f, 0.45f},
                     HudColor{0.04f, 0.04f, 0.04f, 0.7f},
                     k_lineDim);
    }
}
