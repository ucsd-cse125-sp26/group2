/// @file DamageNumberWidget.cpp
#include "DamageNumberWidget.hpp"

#include "hud/HudContext.hpp"

#include <cstdio>

DamageNumberWidget::DamageNumberWidget()
{
    anchor = HudAnchor::TopLeft;
    visible = true;
}

void DamageNumberWidget::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    viewProj_ = state.viewProj;
    screenW_ = state.screenW;
    screenH_ = state.screenH;

    // Age existing entries, remove dead ones.
    for (int i = 0; i < count_;) {
        entries_[i].life -= dt;
        entries_[i].driftY += 60.f * dt; // drift upward in world units
        if (entries_[i].life <= 0.f) {
            entries_[i] = entries_[count_ - 1];
            --count_;
        } else {
            ++i;
        }
    }

    // Spawn new entries from this frame's damage events.
    for (const auto& dn : state.damageNumbers) {
        if (count_ >= k_maxEntries)
            break;
        Entry& e = entries_[count_++];
        e.worldX = dn.worldX;
        e.worldY = dn.worldY;
        e.worldZ = dn.worldZ;
        e.driftY = 0.f;
        e.damage = dn.damage;
        e.life = 0.8f;
        e.maxLife = 0.8f;
        if (dn.headshot)
            e.color = HudColor(1.0f, 0.85f, 0.2f, 1.f); // gold
        else if (dn.shielded)
            e.color = HudColor(0.3f, 0.6f, 1.0f, 1.f);  // blue
        else
            e.color = HudColor(1.f, 1.f, 1.f, 1.f);     // white
    }
}

void DamageNumberWidget::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    char buf[16];

    for (int i = 0; i < count_; ++i) {
        const Entry& e = entries_[i];

        // Project world position to clip space.
        glm::vec4 clip = viewProj_ * glm::vec4(e.worldX, e.worldY + e.driftY, e.worldZ, 1.f);
        if (clip.w <= 0.f)
            continue; // behind camera

        // NDC → screen.
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        // Vulkan NDC: Y points down (0 top, 1 bottom after viewport).
        const float sx = (ndcX * 0.5f + 0.5f) * screenW_;
        const float sy = (1.f - (ndcY * 0.5f + 0.5f)) * screenH_;

        // Alpha fade based on remaining life.
        const float t = e.life / e.maxLife;
        const float alpha = t * t; // ease-in fade out

        HudColor color = e.color;
        color.a *= alpha;

        const float fontSize = 18.f * uiScale_;
        std::snprintf(buf, sizeof(buf), "%d", e.damage);

        // Dark outline for readability.
        const float outOff = 1.5f * uiScale_;
        HudColor shadow(0.f, 0.f, 0.f, 0.6f * alpha);
        ctx.text(buf, sx + outOff, sy + outOff, fontSize, shadow, HudAlign::Center);
        ctx.text(buf, sx, sy, fontSize, color, HudAlign::Center);
    }
}
