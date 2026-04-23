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
};
