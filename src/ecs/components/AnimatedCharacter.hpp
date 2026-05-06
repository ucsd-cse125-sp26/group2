/// @file AnimatedCharacter.hpp
/// @brief ECS component wiring an entity to its per-entity CharacterAnimator.

#pragma once

#include "client/animation/CharacterAnimator.hpp"

#include <memory>

/// @brief ECS component for an animated, skin-rendered character.
///
/// Attached to every player entity (local + remote).  Holds a non-copyable
/// CharacterAnimator (owns ozz sampling/blending contexts) via a `unique_ptr`
/// so the component remains move-only — EnTT requires components to be
/// movable but not copyable.
///
/// `modelIndex` is a PER-ENTITY clone of the shared rig's `LoadedModel`
/// template — each entity gets its own vertex buffer to stream skinned
/// vertices into, so N animated characters can coexist without fighting
/// over a single renderer slot.
struct AnimatedCharacter
{
    std::unique_ptr<CharacterAnimator> animator; ///< Per-entity sampling/blending/skinning state.
    int modelIndex = -1;                         ///< Renderer model index for this entity's skinned vertex buffer.

    // Animation tick decoupling (perf Phase 3c).
    //
    // ozz pose sampling + skin-matrix flatten costs ~10-20 µs per character.
    // Running it every render frame (potentially 1000+ FPS) is pure waste —
    // human eyes can't tell the difference between 30 Hz and 1000 Hz pose
    // updates as long as the world transform interpolates smoothly between
    // samples.  We accumulate render-thread frame time per character and
    // only call animator->update() when enough time has passed for a fresh
    // sample (default cadence in Game.cpp).  Initialised to a large value so
    // the very first frame always samples.
    float animationAccumulator = 1.0f; ///< Seconds since last animator->update().
};
