/// @file EnemyWorldHealthBar.hpp
/// @brief Floating HP/shield bar above each on-screen enemy.
///
/// Mirrors `EnemyHpBar` from the prototype: name + numeric on top, optional
/// thin cyan shield bar, then a thicker red HP bar with the same ghost-trail
/// drain animation as the local-player vitals.  Each enemy in the
/// `HudGameState::worldEnemies` span is projected to screen space; bars are
/// drawn only when the projected point is in front of the camera and inside
/// the viewport.

#pragma once

#include "hud/HudWidget.hpp"

#include <glm/mat4x4.hpp>
#include <string>
#include <unordered_map>

struct EnemyWorldHealthBar : HudWidget
{
    float barWidth = 140.f;
    float shieldHeight = 3.f;
    float healthHeight = 6.f;
    float fontSize = 11.f;
    float yOffsetPx = 12.f; ///< Pixels above the projected world point.

    EnemyWorldHealthBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct EnemyState
    {
        float worldX = 0.f, worldY = 0.f, worldZ = 0.f;
        std::string name;
        int hp = 100, maxHp = 100;
        int sh = 0, maxSh = 100;
        int displayHp = 100;
        int displaySh = 0;
        float trailHp = 1.f;
        float trailSh = 0.f;
        float trailHpHold = 0.f;
        float trailShHold = 0.f;
        float liveHp = 1.f;
        float liveSh = 0.f;
        bool alive = true;
    };

    glm::mat4 viewProj_{1.f};
    float screenW_ = 1280.f, screenH_ = 720.f;

    /// @brief Per-enemy state keyed by name (stable across frames).  Held
    ///        across frames so ghost trails persist between updates.
    std::unordered_map<std::string, EnemyState> enemies_;
};
