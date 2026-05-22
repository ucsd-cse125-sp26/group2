/// @file KinematicCharacterController.hpp
/// @brief Focused capsule KCC step for player/world collision resolution.

#pragma once

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/physics/KccFrameResult.hpp"
#include "ecs/physics/SweptCollision.hpp"

#include <entt/entity/fwd.hpp>
#include <glm/vec3.hpp>

struct PlayerSimState;

namespace systems
{

/// @brief Resolve one fixed-tick capsule character-controller step.
///
/// MovementSystem owns intent and velocity shaping. This module owns the
/// collision-side KCC work: capsule depenetration, walk-capsule horizontal
/// sweep, ground snap, vertical sweep, final grounded state, and diagnostics.
physics::KccFrameResult runKinematicCharacterController(glm::vec3& pos,
                                                        glm::vec3& vel,
                                                        const CollisionShape& shape,
                                                        PlayerVisState& state,
                                                        float dt,
                                                        const physics::WorldGeometry& world,
                                                        entt::entity entity,
                                                        bool jumpedThisTick,
                                                        PlayerSimState* simState = nullptr);

} // namespace systems
