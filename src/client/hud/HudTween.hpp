/// @file HudTween.hpp
/// @brief Lightweight fixed-pool tween engine for HUD animations.

#pragma once

#include <cstdint>

/// @brief Easing function signature: maps t ∈ [0,1] → [0,1].
using HudEaseFn = float (*)(float t);

// ── Built-in easing functions ───────────────────────────────────────────────
float easeLinear(float t);
float easeInQuad(float t);
float easeOutQuad(float t);
float easeInOutQuad(float t);
float easeOutBack(float t);
float easeOutElastic(float t);

// ── Tween pool ──────────────────────────────────────────────────────────────

/// @brief One active interpolation targeting a float.
struct HudTweenEntry
{
    float* target = nullptr;
    float from = 0.f;
    float to = 0.f;
    float duration = 0.f;
    float elapsed = 0.f;
    HudEaseFn ease = easeOutQuad;
    bool active = false;
};

/// @brief Fixed-size tween pool.  No heap allocations.
class HudTweenPool
{
public:
    static constexpr int k_maxTweens = 64;

    /// @brief Start or replace a tween on *target* from its current value to *to*.
    void tween(float* target, float to, float duration, HudEaseFn ease = easeOutQuad);

    /// @brief Start or replace a tween on *target* from *from* to *to*.
    void tween(float* target, float from, float to, float duration, HudEaseFn ease = easeOutQuad);

    /// @brief Cancel any active tween targeting *target*.
    void cancel(float* target);

    /// @brief Tick all active tweens by *dt* seconds.
    void update(float dt);

private:
    HudTweenEntry entries_[k_maxTweens] = {};

    /// @brief Find an existing tween on target, or the first free slot.
    HudTweenEntry* findSlot(float* target);
};
