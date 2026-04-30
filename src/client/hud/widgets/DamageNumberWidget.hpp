/// @file DamageNumberWidget.hpp
/// @brief Floating in-world damage numbers projected to screen space.

#pragma once

#include "hud/HudWidget.hpp"

#include <array>
#include <glm/mat4x4.hpp>

/// @brief Displays floating damage numbers at world-space hit positions.
///
/// Each number drifts upward and fades out over ~0.8s.  Colors:
///   - Gold   for headshots.
///   - Blue   for shielded (armor) hits.
///   - White  for health-only hits.
struct DamageNumberWidget : HudWidget
{
    DamageNumberWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Entry
    {
        float worldX, worldY, worldZ; ///< Original hit position.
        float driftY = 0.f;           ///< Accumulated upward drift.
        int damage = 0;
        HudColor color;
        float life = 0.f;    ///< Remaining lifetime.
        float maxLife = 0.f; ///< Initial lifetime (for alpha calc).
    };

    static constexpr int k_maxEntries = 32;
    std::array<Entry, k_maxEntries> entries_{};
    int count_ = 0;

    glm::mat4 viewProj_{1.f};
    float screenW_ = 1280.f;
    float screenH_ = 720.f;
};
