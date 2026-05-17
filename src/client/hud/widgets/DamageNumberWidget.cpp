/// @file DamageNumberWidget.cpp
#include "DamageNumberWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

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
    for (size_t i = 0; i < count_;) {
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
        // Voidfall palette: red headshots, cyan shield, amber on-hp.
        if (dn.headshot)
            e.color = voidfall::k_red;
        else if (dn.shielded)
            e.color = voidfall::k_cyan;
        else
            e.color = voidfall::k_amber;
    }
}

void DamageNumberWidget::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    char buf[16];

    for (size_t i = 0; i < count_; ++i) {
        const Entry& e = entries_[i];

        // Project world position to clip space.
        glm::vec4 clip = viewProj_ * glm::vec4(e.worldX, e.worldY + e.driftY, e.worldZ, 1.f);
        if (clip.w <= 0.f)
            continue; // behind camera

        // NDC → screen.
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        // Projection already flips Y for Vulkan, so NDC maps directly:
        // ndcY -1 = top, +1 = bottom → screen Y = (ndcY*0.5+0.5) * screenH.
        float sx = (ndcX * 0.5f + 0.5f) * screenW_;
        float sy = (ndcY * 0.5f + 0.5f) * screenH_;

        // Offset to the right of the hit point so numbers don't obscure the target.
        sx += 40.f * uiScale_;

        // Alpha fade based on remaining life.
        const float t = e.life / e.maxLife;
        const float alpha = t * t; // ease-in fade out

        HudColor color = e.color;
        color.a *= alpha;

        // Design uses bigger numerals for headshots and kills.
        // We don't know "kill" here (it's per-shot, not per-frame), so the
        // distinction is headshot=22 px, regular=18 px (matches prototype).
        const float baseSize = (e.color.r > 0.85f && e.color.g < 0.45f) ? 22.f : 18.f;
        const float fontSize = baseSize * uiScale_;
        // Voidfall convention: damage numbers are negative deltas.  Prototype
        // shows "−48" with a minus glyph, so prefix accordingly.
        std::snprintf(buf, sizeof(buf), "-%d", e.damage);

        // Single SDF text draw with outline-on — the shader paints a 1-px
        // dark band tight to the glyph for readability over the world.
        // Replaces the previous manual offset-shadow (which produced a
        // double-vision halo at small sizes).
        ctx.text(buf, sx, sy, fontSize, color, HudAlign::Left, /*outlined=*/true);
    }
}
