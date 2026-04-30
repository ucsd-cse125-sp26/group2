/// @file HudTween.cpp
/// @brief Tween pool + easing function implementations.

#include "HudTween.hpp"

#include <algorithm>
#include <cmath>

// ── Easing functions ────────────────────────────────────────────────────────

float easeLinear(float t)
{
    return t;
}

float easeInQuad(float t)
{
    return t * t;
}

float easeOutQuad(float t)
{
    return t * (2.f - t);
}

float easeInOutQuad(float t)
{
    return t < 0.5f ? 2.f * t * t : -1.f + (4.f - 2.f * t) * t;
}

float easeOutBack(float t)
{
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float tm1 = t - 1.f;
    return 1.f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
}

float easeOutElastic(float t)
{
    if (t <= 0.f)
        return 0.f;
    if (t >= 1.f)
        return 1.f;
    constexpr float c4 = (2.f * 3.14159265f) / 3.f;
    return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - 0.75f) * c4) + 1.f;
}

// ── HudTweenPool ────────────────────────────────────────────────────────────

HudTweenEntry* HudTweenPool::findSlot(float* target)
{
    // First pass: find existing tween on this target.
    for (auto& e : entries_)
        if (e.active && e.target == target)
            return &e;

    // Second pass: find a free slot.
    for (auto& e : entries_)
        if (!e.active)
            return &e;

    return nullptr; // Pool full — silently drop.
}

void HudTweenPool::tween(float* target, float to, float duration, HudEaseFn ease)
{
    tween(target, *target, to, duration, ease);
}

void HudTweenPool::tween(float* target, float from, float to, float duration, HudEaseFn ease)
{
    HudTweenEntry* slot = findSlot(target);
    if (!slot)
        return;

    slot->target = target;
    slot->from = from;
    slot->to = to;
    slot->duration = std::max(duration, 0.001f);
    slot->elapsed = 0.f;
    slot->ease = ease ? ease : easeLinear;
    slot->active = true;
    *target = from;
}

void HudTweenPool::cancel(float* target)
{
    for (auto& e : entries_)
        if (e.active && e.target == target)
            e.active = false;
}

void HudTweenPool::update(float dt)
{
    for (auto& e : entries_) {
        if (!e.active)
            continue;

        e.elapsed += dt;
        if (e.elapsed >= e.duration) {
            *e.target = e.to;
            e.active = false;
        } else {
            const float t = e.ease(e.elapsed / e.duration);
            *e.target = e.from + (e.to - e.from) * t;
        }
    }
}
