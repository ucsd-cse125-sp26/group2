/// @file RagdollSystem.hpp
/// @brief Spawn / despawn ragdolls and tick their per-frame state.
///
/// `spawnRagdoll(character)` allocates 15 capsule rigid bodies sized to
/// match a typical 72-unit Quake-style character, links them with point
/// / hinge / cone-twist joints, and attaches the parent `Ragdoll`
/// component to the character entity.
///
/// The character's `Velocity` (linear) and `AngularVelocity` (if present)
/// are inherited by the torso so the corpse keeps the player's pre-death
/// motion — feels much better than a stationary spawn.
///
/// On settle (all bodies asleep), `runRagdolls()` increments `age` and
/// gameplay code can decide when to fade the corpse out.

#pragma once

#include "ecs/registry/Registry.hpp"

namespace systems
{

/// @brief Build a 15-body humanoid ragdoll for the dead character.
/// The character's existing position / velocity become the seed state
/// for the new bodies.  Idempotent: calling twice on the same character
/// is a no-op (the second call sees the `Ragdoll` component is already
/// present).
///
/// @return The created `Ragdoll` parent entity (== `character`).
entt::entity spawnRagdoll(Registry& registry, entt::entity character);

/// @brief Destroy the ragdoll bodies and joints owned by a character.
///
/// Called when the player respawns or disconnects. This removes the
/// transient physics entities immediately instead of letting every death
/// leave orphan registry entries behind.
void destroyRagdoll(Registry& registry, entt::entity character);

/// @brief Per-tick ragdoll bookkeeping: advance `age`, optionally tick
/// the cleanup timer for old corpses.  Called from the same tick that
/// runs the constraint solver.
///
/// @param registry  ECS registry.
/// @param dt        Tick duration in seconds.
void runRagdolls(Registry& registry, float dt);

} // namespace systems
