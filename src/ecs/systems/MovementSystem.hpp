/// @file MovementSystem.hpp
/// @brief Shared movement system implementing the Titanfall-inspired state machine.

#pragma once

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/physics/KccFrameResult.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"

struct PlayerVisState;
struct PlayerSimState;
struct ConstPlayerStateRef;

/// @brief Shared movement system — compiled identically on client and server.
///
/// Any divergence between client and server builds is a bug (breaks prediction).
///
/// Implements the Titanfall-inspired movement state machine:
///   OnFoot → Sliding → WallRunning
/// with sprint, double jump, coyote time, jump lurch, air strafing, and speed cap.
///
/// @note Position integration is NOT done here — CollisionSystem owns that via swept AABB.
namespace systems
{

/// @brief Apply one tick of player movement physics to all eligible entities.
///
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
/// @param world     World collision geometry (needed for wall detection).
void runMovement(Registry& registry, float dt, const physics::WorldGeometry& world);

/// @brief Consume collision-owned KCC feedback after position integration.
///
/// MovementSystem owns traversal state. CollisionSystem calls this immediately
/// after KCC so wallrun can respond to blockers, ceilings, and solver stalls
/// before the next movement tick accelerates again.
void reconcileMovementAfterKcc(glm::vec3& pos,
                               glm::vec3& vel,
                               const CollisionShape& shape,
                               PlayerVisState& vis,
                               PlayerSimState& sim,
                               const InputSnapshot& input,
                               const physics::WorldGeometry& world,
                               const physics::KccFrameResult& kcc,
                               float dt);

/// @brief Determine the current ground wish speed based on movement mode and stance.
///
/// Returns the speed the player is accelerating toward on the ground this tick:
/// `tms::k_crouchSpeed` / `k_sprintSpeed` / `k_walkSpeed`, or `0` during a slide.
/// For air movement use `physics::k_airMaxSpeed` directly.
///
/// @param vis  Replicated player vis state (for moveMode + crouching + sprinting).
/// @return Target ground wish speed (u/s).
float currentWishSpeed(const PlayerVisState& vis);

} // namespace systems
