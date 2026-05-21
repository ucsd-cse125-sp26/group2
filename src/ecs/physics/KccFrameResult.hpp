/// @file KccFrameResult.hpp
/// @brief Per-tick collision feedback emitted by the player KCC.

#pragma once

#include <glm/vec3.hpp>

namespace physics
{

/// @brief Collision-owned result for one player KCC step.
///
/// MovementSystem consumes this after collision to decide traversal policy
/// without duplicating sweep/depenetration logic.
struct KccFrameResult
{
    glm::vec3 posBefore{0.0f};
    glm::vec3 posAfter{0.0f};
    glm::vec3 velBefore{0.0f};
    glm::vec3 velAfter{0.0f};
    glm::vec3 attemptedDelta{0.0f};
    glm::vec3 actualDelta{0.0f};
    glm::vec3 depenDelta{0.0f};

    glm::vec3 firstHitNormal{0.0f};
    glm::vec3 lastHitNormal{0.0f};
    glm::vec3 blockerNormal{0.0f};
    glm::vec3 ceilingNormal{0.0f};
    glm::vec3 floorNormal{0.0f};

    float progressRatio{1.0f};
    float depenPushDistance{0.0f};
    int bumpHits{0};
    int caIterations{0};
    int sweepHits{0};

    bool usedWalkCapsule{false};
    bool caExhausted{false};
    bool hitFloor{false};
    bool hitCeiling{false};
    bool hitWall{false};
    bool hitBlocker{false};
    bool resolvedOscillation{false};
};

} // namespace physics
