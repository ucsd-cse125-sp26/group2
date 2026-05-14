/// @file Ragdoll.hpp
/// @brief Ragdoll components — bone-to-rigid-body mapping for character death.
///
/// On character death the gameplay code calls `spawnRagdoll(...)` which
/// allocates 15 rigid bodies (one per major bone) and the joints connecting
/// them.  The new bodies are owned by a `Ragdoll` parent component on the
/// character entity; destroying the character also destroys all its
/// ragdoll children.
///
/// Renderer integration: each ragdoll body carries a `RagdollBoneTag`
/// linking it back to the source character + bone index; the skinning
/// system reads the body's `Position + Orientation` and pipes them into
/// the bone palette for that character.

#pragma once

#include <array>
#include <cstdint>
#include <entt/entt.hpp>

/// @brief Standard humanoid bone indices for the 15-body ragdoll.
/// Order matches the joint table in `Ragdoll::buildHumanoid`.
enum class RagdollBone : uint8_t
{
    Head = 0,
    Torso = 1,
    Pelvis = 2,
    UpperArmL = 3,
    UpperArmR = 4,
    ForearmL = 5,
    ForearmR = 6,
    HandL = 7,
    HandR = 8,
    UpperLegL = 9,
    UpperLegR = 10,
    LowerLegL = 11,
    LowerLegR = 12,
    FootL = 13,
    FootR = 14,
    Count = 15,
};

/// @brief Marker on each ragdoll-body entity; points back to the parent
/// ragdoll character and which bone slot this body fills.
struct RagdollBoneTag
{
    entt::entity character{entt::null};
    RagdollBone bone{RagdollBone::Torso};
};

/// @brief Parent component on the dead character; holds the 15 ragdoll
/// body entities and the joint entities connecting them.  Destroying this
/// component (or the entity holding it) should be paired with destroying
/// every body / joint listed here.
struct Ragdoll
{
    std::array<entt::entity, static_cast<size_t>(RagdollBone::Count)> bodies{};
    std::array<entt::entity, 14u> joints{}; ///< 14 joints for 15 bones in tree topology.

    /// @brief Seconds since ragdoll spawn.  Used by gameplay to fade out
    /// the corpse after settle.
    float age = 0.0f;
};
