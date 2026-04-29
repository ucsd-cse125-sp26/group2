/// @file Hitbox.hpp
/// @brief Skeleton-driven hitbox types: body regions, capsule definitions, damage profiles,
///        and per-entity runtime state.
///
/// Modern FPS hitbox system (CS2/Valorant/Deadlock style):
///  - Capsule volumes attached to skeleton bones
///  - Transforms follow animation pose every tick
///  - Different body regions carry different damage multipliers

#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Body regions
// ---------------------------------------------------------------------------

/// @brief Body region identifiers for damage multiplier lookup.
enum class BodyRegion : uint8_t
{
    Head,
    Neck,
    UpperTorso,
    LowerTorso,
    LeftUpperArm,
    LeftLowerArm,
    RightUpperArm,
    RightLowerArm,
    LeftUpperLeg,
    LeftLowerLeg,
    RightUpperLeg,
    RightLowerLeg,
    Count
};

/// @brief Human-readable name for a body region (UI / kill feed / logging).
inline const char* bodyRegionName(BodyRegion region)
{
    switch (region) {
    case BodyRegion::Head:
        return "Head";
    case BodyRegion::Neck:
        return "Neck";
    case BodyRegion::UpperTorso:
        return "Upper Torso";
    case BodyRegion::LowerTorso:
        return "Lower Torso";
    case BodyRegion::LeftUpperArm:
        return "L Upper Arm";
    case BodyRegion::LeftLowerArm:
        return "L Lower Arm";
    case BodyRegion::RightUpperArm:
        return "R Upper Arm";
    case BodyRegion::RightLowerArm:
        return "R Lower Arm";
    case BodyRegion::LeftUpperLeg:
        return "L Upper Leg";
    case BodyRegion::LeftLowerLeg:
        return "L Lower Leg";
    case BodyRegion::RightUpperLeg:
        return "R Upper Leg";
    case BodyRegion::RightLowerLeg:
        return "R Lower Leg";
    case BodyRegion::Count:
        return "(none)";
    }
    return "(unknown)";
}

// ---------------------------------------------------------------------------
// Hitbox definition (static, per-rig)
// ---------------------------------------------------------------------------

/// @brief One bone-attached collision capsule in the rig's bone-local space.
///
/// The capsule centerline runs from `localOffset - localAxis*halfHeight`
/// to `localOffset + localAxis*halfHeight`.  Total swept length =
/// `2*halfHeight + 2*radius` (capsule = Minkowski sum of segment + sphere).
struct HitboxDef
{
    std::string boneName;                       ///< Skeleton joint name (e.g. "mixamorig:Head").
    int boneIndex = -1;                         ///< Resolved runtime joint index (-1 = unresolved).
    BodyRegion region = BodyRegion::UpperTorso; ///< Which body part this covers.
    glm::vec3 localOffset{0.0f};                ///< Offset from bone origin in bone-local space.
    float radius = 4.0f;                        ///< Capsule radius (rig model-space units).
    float halfHeight = 4.0f;                    ///< Capsule half-height of centerline.
    glm::vec3 localAxis{0.0f, 1.0f, 0.0f};      ///< Capsule axis direction in bone-local space.
};

// ---------------------------------------------------------------------------
// Runtime capsule (world-space, per-entity, per-frame)
// ---------------------------------------------------------------------------

/// @brief World-space capsule, recomputed each tick from animation pose + entity transform.
struct WorldCapsule
{
    glm::vec3 pointA{0.0f};                     ///< One endpoint of the capsule centerline.
    glm::vec3 pointB{0.0f};                     ///< Other endpoint of the capsule centerline.
    float radius = 0.0f;                        ///< Capsule radius (world units).
    BodyRegion region = BodyRegion::UpperTorso; ///< For damage multiplier lookup.
};

// ---------------------------------------------------------------------------
// ECS components
// ---------------------------------------------------------------------------

/// @brief ECS component: resolved hitbox capsules for one entity this frame.
///
/// Written by `systems::updateHitboxes()`, read by the weapon raycast system.
struct HitboxInstance
{
    std::vector<WorldCapsule> capsules; ///< ~12 capsules per character.
};

/// @brief ECS component: per-entity model-space joint matrices from animation.
///
/// Populated by the animation system (client: CharacterAnimator, server: same
/// animator class without skinning), consumed by HitboxSystem.
struct JointMatrices
{
    std::vector<glm::mat4> matrices; ///< Model-space joint transforms, one per skeleton joint.
};

// ---------------------------------------------------------------------------
// Damage profile
// ---------------------------------------------------------------------------

/// @brief Damage multiplier table, indexed by BodyRegion.
struct DamageProfile
{
    // clang-format off
    std::array<float, static_cast<size_t>(BodyRegion::Count)> multipliers = {{
        4.00f,  // Head
        2.00f,  // Neck
        1.00f,  // UpperTorso
        1.00f,  // LowerTorso
        0.90f,  // LeftUpperArm
        0.90f,  // LeftLowerArm
        0.90f,  // RightUpperArm
        0.90f,  // RightLowerArm
        0.75f,  // LeftUpperLeg
        0.75f,  // LeftLowerLeg
        0.75f,  // RightUpperLeg
        0.75f,  // RightLowerLeg
    }};
    // clang-format on
};

/// @brief Global (default) damage profile accessor.
inline const DamageProfile& defaultDamageProfile()
{
    static const DamageProfile profile;
    return profile;
}

// ---------------------------------------------------------------------------
// Hitbox rig (shared per character archetype)
// ---------------------------------------------------------------------------

/// @brief Collection of hitbox definitions for a character rig.
///
/// Built once at startup.  All entities using the same skeleton share one
/// HitboxRig instance.  Call `resolveIndices()` after loading the skeleton
/// to map bone names to runtime joint indices.
struct HitboxRig
{
    std::vector<HitboxDef> definitions;

    /// @brief Build the default Mixamo-rig hitbox definitions (12 capsules).
    ///
    /// Capsule dimensions are in the rig's model space (before `rigScale` is
    /// applied).  Values are tuned for a standard Mixamo mannequin (~170 cm
    /// tall in model space).
    static HitboxRig buildMixamoDefault()
    {
        HitboxRig rig;
        auto& defs = rig.definitions;
        defs.reserve(12);

        // clang-format off
        // NOTE: In Mixamo rigs imported via assimp/ozz the bone-local +Y axis
        // points from child toward parent (i.e. "up" for legs, "inward" for arms).
        // Therefore offsets that push the capsule ALONG the bone (toward the child
        // joint) use negative Y.  The localAxis {0,-1,0} makes halfHeight extend
        // in the -Y direction (along the bone) so the capsule covers from the bone
        // origin toward the next joint.
        //                boneName                        region                       offset                  radius  halfH   axis
        defs.push_back({"mixamorig:Head",          -1, BodyRegion::Head,          {0, -4, 0},            4.5f,  3.0f,  {0,-1,0}});
        defs.push_back({"mixamorig:Neck",          -1, BodyRegion::Neck,          {0,  0, 0},            3.0f,  2.0f,  {0,-1,0}});
        defs.push_back({"mixamorig:Spine2",        -1, BodyRegion::UpperTorso,    {0,  0, 0},            8.0f,  6.0f,  {0,-1,0}});
        defs.push_back({"mixamorig:Spine1",        -1, BodyRegion::LowerTorso,    {0,  0, 0},            7.5f,  5.0f,  {0,-1,0}});
        defs.push_back({"mixamorig:LeftArm",       -1, BodyRegion::LeftUpperArm,  {0, 10, 0},            3.5f,  10.0f, {0,-1,0}});
        defs.push_back({"mixamorig:LeftForeArm",   -1, BodyRegion::LeftLowerArm,  {0, 10, 0},            3.0f,  10.0f, {0,-1,0}});
        defs.push_back({"mixamorig:RightArm",      -1, BodyRegion::RightUpperArm, {0, 10, 0},            3.5f,  10.0f, {0,-1,0}});
        defs.push_back({"mixamorig:RightForeArm",  -1, BodyRegion::RightLowerArm, {0, 10, 0},            3.0f,  10.0f, {0,-1,0}});
        defs.push_back({"mixamorig:LeftUpLeg",     -1, BodyRegion::LeftUpperLeg,  {0, 18, 0},            4.5f,  16.0f, {0,-1,0}});
        defs.push_back({"mixamorig:LeftLeg",       -1, BodyRegion::LeftLowerLeg,  {0, 18, 0},            3.5f,  16.0f, {0,-1,0}});
        defs.push_back({"mixamorig:RightUpLeg",    -1, BodyRegion::RightUpperLeg, {0, 18, 0},            4.5f,  16.0f, {0,-1,0}});
        defs.push_back({"mixamorig:RightLeg",      -1, BodyRegion::RightLowerLeg, {0, 18, 0},            3.5f,  16.0f, {0,-1,0}});
        // clang-format on

        return rig;
    }

    /// @brief Resolve bone names to runtime joint indices.
    /// @param jointMap  Bone name -> index mapping from CharacterRig::jointMap().
    void resolveIndices(const std::unordered_map<std::string, int>& jointMap)
    {
        for (auto& def : definitions) {
            auto it = jointMap.find(def.boneName);
            if (it != jointMap.end()) {
                def.boneIndex = it->second;
            } else {
                def.boneIndex = -1;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Debug: hit detection snapshot (for client-server mismatch debugging)
// ---------------------------------------------------------------------------

/// @brief Snapshot captured on the client when a local hitscan detects a hit.
///
/// If the server later rejects this hit, the server's capsule state is filled
/// in for side-by-side comparison in the debug UI.
struct HitDebugSnapshot
{
    uint32_t tick = 0;                              ///< Tick when the shot was fired.
    glm::vec3 shooterEye{0.0f};                     ///< Ray origin.
    glm::vec3 shooterDir{0.0f};                     ///< Ray direction.
    glm::vec3 targetPos{0.0f};                      ///< Target entity position at time of shot.
    float targetYaw = 0.0f;                         ///< Target yaw at time of shot.
    std::vector<WorldCapsule> clientCapsules;       ///< Client-side hitbox state.
    std::vector<WorldCapsule> serverCapsules;       ///< Server-side state (filled on rejection).
    BodyRegion clientHitRegion = BodyRegion::Count; ///< What the client thought it hit.
    bool serverConfirmed = false;                   ///< True if server confirmed the hit.
    bool serverRejected = false;                    ///< True if server rejected the hit.
};
