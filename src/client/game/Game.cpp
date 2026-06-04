/// @file Game.cpp
/// @brief Implementation of the top-level Game class and SDL application callbacks.

#include "Game.hpp"

#include "SDL3/SDL_init.h"
#include "animation/CharacterAnimator.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <ozz/animation/runtime/skeleton.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include "config/InputBindings.hpp"
#include "ecs/AssetCatalog.hpp"
#include "ecs/MapConfig.hpp"
#include "ecs/abilities/AbilityTuning.hpp"
#include "ecs/components/AbilityState.hpp"
#include "ecs/components/AnimatedCharacter.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Controllable.hpp"
#include "ecs/components/DeathInfo.hpp"
#include "ecs/components/DroppedWeapon.hpp"
#include "ecs/components/FireField.hpp"
#include "ecs/components/GrenadeState.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/HealthPackSpawner.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/Orientation.hpp"
#include "ecs/components/PlayerColor.hpp"
#include "ecs/components/PlayerColors.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerName.hpp"
#include "ecs/components/PlayerSimState.hpp" // also pulls in PlayerVisState
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PowerupSpawner.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/components/Ragdoll.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/ViewmodelConfig.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/DebugCollisionDraw.hpp"
#include "ecs/physics/PhaseDiagnostic.hpp"
#include "ecs/physics/PhysicsPerfStats.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/systems/AbilitySystem.hpp"
#include "ecs/systems/HitboxSystem.hpp"
#include "ecs/systems/PickupGeometry.hpp"
#include "hud/VoidfallStyle.hpp"
#include "hud/debug/HudDebugPanel.hpp"
#include "menus/MenuTheme.hpp"
#include "network/EntityInterpolation.hpp"
#include "network/RosterEvent.hpp"
#include "network/ShotEvent.hpp"
#include "particles/ParticleEvents.hpp"
#include "renderer-new/Asset.hpp"
#include "renderer-new/GraphicsConfig.hpp"
#include "renderer-new/RendererTypes.hpp"
#include "systems/InputSampleSystem.hpp"
#include "systems/InputSendSystem.hpp"
#include "systems/PredictionSystem.hpp"
#include "systems/ReconciliationSystem.hpp"
#include "util/InputCapture.hpp"

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <imgui.h>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace
{
constexpr float kMinFootstepIntervalSeconds = 0.14f;
constexpr std::array<const char*, 2> kOptionalTestModelFilenames{{"test_model.glb", "test model.glb"}};
constexpr glm::vec3 kOptionalTestModelRifleSpawnerOffset{0.0f, 50.0f, 0.0f};
constexpr float kOptionalTestModelLoadScale = 10.0f;
constexpr std::array<const char*, kRenderableWeaponTypeCount> kRenderableWeaponNames{
    "Rifle", "Rocket", "RailGun", "EnergyGun", "Shotgun"};
constexpr std::array<const char*, kRenderableWeaponTypeCount> kRenderableWeaponDisplayNames{
    "Rifle (R-301)", "Rocket", "RailGun (Triple Take)", "EnergyGun (Wingman)", "Shotgun"};
struct DroppedWeaponRenderableTag
{};

// (kThirdPersonWeaponPitchMax was used by the removed buildThirdPersonWeaponAttachment;
// the bone-parented weapon path doesn't pitch-clamp the weapon — the spine bend
// max-pitch in CharacterAnimator handles that.)
constexpr std::array<const char*, kHandFingerMountCount> kModelLeftFingerMountNames{
    {"ik_l_thumb_tip", "ik_l_index_tip", "ik_l_middle_tip", "ik_l_ring_tip", "ik_l_pinky_tip"}};
constexpr std::array<const char*, kHandFingerMountCount> kModelRightFingerMountNames{
    {"ik_r_thumb_tip", "ik_r_index_tip", "ik_r_middle_tip", "ik_r_ring_tip", "ik_r_pinky_tip"}};

int addAssetDefinition(AssetRegistry& assets, const AssetDefinition& def)
{
    return assets.add(
        def.name, def.filename, def.role, def.renderScale, def.renderTranslation, def.renderRotationDegrees);
}

std::filesystem::path assetPathFor(const char* filename)
{
    const char* const base = SDL_GetBasePath();
    std::filesystem::path path = base ? base : "";
    path /= ASSETS_DIR;
    path /= filename;
    return path;
}

const char* findExistingOptionalTestModelFilename()
{
    for (const char* filename : kOptionalTestModelFilenames) {
        std::error_code ec;
        if (std::filesystem::exists(assetPathFor(filename), ec))
            return filename;
    }
    return nullptr;
}

std::optional<glm::vec3> findOptionalTestModelPosition()
{
    for (const gamemap::WeaponSpawner& spawner : gamemap::weaponSpawner_) {
        if (spawner.type == WeaponType::Rifle)
            return spawner.pos + kOptionalTestModelRifleSpawnerOffset;
    }
    return std::nullopt;
}

WeaponSpawnerModelParams defaultSpawnerModelParams(WeaponType type)
{
    return getWeaponSpawnerModelParams(type);
}

bool isRenderableGunType(WeaponType type)
{
    return static_cast<std::size_t>(type) < kWeaponAssets.size();
}

/// @brief Apply a recoil aim delta to a player's look angles safely.
///
/// Guarantees `snap.pitch`/`snap.yaw` stay finite and that pitch stays inside
/// the gimbal-lock-safe range, regardless of the delta or the pre-existing
/// value. A non-finite delta is ignored, and a non-finite base self-heals to 0.
/// This makes it impossible for a single inf/NaN from any recoil source to
/// permanently wedge the aim — the failure that previously showed up as pitch
/// pinned to a huge number and yaw = NaN.
void applyRecoilAimDelta(InputSnapshot& snap, float dPitch, float dYaw)
{
    float pitch = std::isfinite(snap.pitch) ? snap.pitch : 0.0f;
    if (std::isfinite(dPitch))
        pitch += dPitch;
    snap.pitch = std::clamp(pitch, -glm::radians(89.0f), glm::radians(89.0f));

    float yaw = std::isfinite(snap.yaw) ? snap.yaw : 0.0f;
    if (std::isfinite(dYaw))
        yaw += dYaw;
    // Keep yaw bounded so it can never drift toward the limits of float range.
    snap.yaw = std::remainder(yaw, glm::two_pi<float>());
}

const char* renderableWeaponDisplayName(WeaponType type)
{
    const std::size_t idx = static_cast<std::size_t>(type);
    if (idx >= kRenderableWeaponDisplayNames.size())
        return "Unsupported";
    return kRenderableWeaponDisplayNames[idx];
}

glm::quat spawnerModelRotation(const WeaponSpawnerModelParams& params, float timeSeconds, bool active)
{
    const glm::vec3 r = glm::radians(glm::vec3{params.pitchOffset, params.yawOffset, params.rollOffset});
    const float spin = active ? glm::radians(params.spinDegreesPerSecond) * timeSeconds : 0.0f;
    return glm::angleAxis(spin + r.y, glm::vec3{0.0f, 1.0f, 0.0f}) * glm::angleAxis(r.x, glm::vec3{1.0f, 0.0f, 0.0f}) *
           glm::angleAxis(r.z, glm::vec3{0.0f, 0.0f, 1.0f});
}

glm::mat4 weaponRotationMatrix(float yawDegrees, float pitchDegrees, float rollDegrees)
{
    return glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3{0, 1, 0}) *
           glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3{1, 0, 0}) *
           glm::rotate(glm::mat4(1.0f), glm::radians(rollDegrees), glm::vec3{0, 0, 1});
}

glm::vec3 transformDirection(const glm::mat4& basis, const glm::vec3& direction)
{
    return glm::vec3(basis * glm::vec4(direction, 0.0f));
}

const Asset::Model* modelFromRendererIndex(int modelIndex)
{
    if (modelIndex < 0 || static_cast<std::size_t>(modelIndex) >= Asset::modelInstances_.size())
        return nullptr;

    const ModelIdInt modelId = Asset::modelInstances_[static_cast<std::size_t>(modelIndex)].modelId_;
    const auto modelIt = Asset::models_.find(modelId);
    if (modelIt == Asset::models_.end())
        return nullptr;

    return &modelIt->second;
}

const Asset::MountPoint* findModelMountPoint(const Asset::Model* model, const char* name)
{
    if (model == nullptr)
        return nullptr;

    const auto mountIt = model->mountPoints.find(name);
    if (mountIt == model->mountPoints.end())
        return nullptr;

    return &mountIt->second;
}

glm::mat4 handMountRotation(const HandMountPoint& mount)
{
    return weaponRotationMatrix(mount.rotationDegrees.y, mount.rotationDegrees.x, mount.rotationDegrees.z);
}

glm::vec3 mountRotationDegrees(const Asset::MountPoint& mount)
{
    return glm::degrees(glm::eulerAngles(mount.rotation));
}

glm::vec3 firstPersonMountOffset(const Asset::MountPoint& mount, float weaponScale)
{
    return glm::vec3{-mount.position.x * weaponScale, mount.position.y * weaponScale, mount.position.z * weaponScale};
}

void makeFingerOffsetsPalmRelative(FirstPersonArmMountSet& arm)
{
    for (HandMountPoint& finger : arm.fingers)
        finger.offset -= arm.palm.offset;
}

void makeFingerOffsetsPalmRelative(FirstPersonHandMountParams& mounts)
{
    makeFingerOffsetsPalmRelative(mounts.rightArm);
    makeFingerOffsetsPalmRelative(mounts.leftArm);
}

// (Legacy `WeaponAttachmentPose` + `buildThirdPersonWeaponAttachment` were
// removed — the weapon is now derived from the right-hand bone matrix post-IK,
// so the player-relative origin/orientation/aim-pivot data they produced was
// completely overridden by the bone-parented pipeline.)

glm::mat4 makeViewmodelHandTransform(const glm::vec3& weaponOrigin,
                                     const glm::mat4& weaponWorld,
                                     const glm::mat4& weaponOrientation,
                                     const Asset::Model* weaponModel,
                                     const char* modelMountName,
                                     const HandMountPoint& mount,
                                     float handScale)
{
    (void)weaponWorld;
    (void)weaponModel;
    (void)modelMountName;
    const glm::vec3 target = weaponOrigin + transformDirection(weaponOrientation, mount.offset);
    glm::mat4 handWorld = glm::translate(glm::mat4(1.0f), target);
    handWorld *= weaponOrientation;
    handWorld *= handMountRotation(mount);
    handWorld = glm::scale(handWorld, glm::vec3(handScale));
    return handWorld;
}

void applyAuthoredFirstPersonHandMountDefaults(const Asset::Model* model,
                                               const ViewmodelParams& vp,
                                               FirstPersonHandMountParams& firstPerson)
{
    if (model == nullptr)
        return;

    auto applyFirstPersonPoint = [&](HandMountPoint& target, const char* name) {
        if (const Asset::MountPoint* mount = findModelMountPoint(model, name); mount != nullptr) {
            target.offset = firstPersonMountOffset(*mount, vp.scale);
            target.rotationDegrees = mountRotationDegrees(*mount);
        }
    };
    auto applyFirstPersonElbow = [&](glm::vec3& target, const char* name) {
        if (const Asset::MountPoint* mount = findModelMountPoint(model, name); mount != nullptr)
            target = firstPersonMountOffset(*mount, vp.scale);
    };
    auto applyFirstPersonFinger = [&](const HandMountPoint& palm, HandMountPoint& finger, const char* name) {
        if (const Asset::MountPoint* mount = findModelMountPoint(model, name); mount != nullptr) {
            finger.offset = firstPersonMountOffset(*mount, vp.scale) - palm.offset;
            finger.rotationDegrees = mountRotationDegrees(*mount);
        }
    };

    applyFirstPersonPoint(firstPerson.rightArm.palm, "ik_r_palm");
    applyFirstPersonElbow(firstPerson.rightArm.elbowOffset, "ik_r_elbow");
    for (size_t i = 0; i < kHandFingerMountCount; ++i) {
        applyFirstPersonFinger(
            firstPerson.rightArm.palm, firstPerson.rightArm.fingers[i], kModelRightFingerMountNames[i]);
    }

    applyFirstPersonPoint(firstPerson.leftArm.palm, "ik_l_palm");
    applyFirstPersonElbow(firstPerson.leftArm.elbowOffset, "ik_l_elbow");
    for (size_t i = 0; i < kHandFingerMountCount; ++i) {
        applyFirstPersonFinger(firstPerson.leftArm.palm, firstPerson.leftArm.fingers[i], kModelLeftFingerMountNames[i]);
    }
}

void copyVec3(std::ostringstream& out, const glm::vec3& value)
{
    out << "{" << value.x << "f, " << value.y << "f, " << value.z << "f}";
}

void copyHandMountPoint(std::ostringstream& out, const HandMountPoint& point)
{
    out << "{.offset = ";
    copyVec3(out, point.offset);
    out << ", .rotationDegrees = ";
    copyVec3(out, point.rotationDegrees);
    out << "}";
}

void copyFirstPersonArmMountSet(std::ostringstream& out, const char* label, const FirstPersonArmMountSet& arm)
{
    out << "." << label << " = {.shoulderOffset = ";
    copyVec3(out, arm.shoulderOffset);
    out << ", .elbowOffset = ";
    copyVec3(out, arm.elbowOffset);
    out << ", .palm = ";
    copyHandMountPoint(out, arm.palm);
    out << ", .fingers = {{\n";
    for (const HandMountPoint& finger : arm.fingers) {
        out << "    ";
        copyHandMountPoint(out, finger);
        out << ",\n";
    }
    out << "}}}";
}

std::string buildFirstPersonHandMountClipboardText(const FirstPersonHandMountParams* mountParams)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "// Current FirstPersonHandMountParams entries\n";
    out << "static const std::array<FirstPersonHandMountParams, kRenderableWeaponTypeCount> k_params{{\n";
    for (std::size_t i = 0; i < kRenderableWeaponTypeCount; ++i) {
        out << "    // " << kRenderableWeaponNames[i] << "\n";
        out << "    {";
        copyFirstPersonArmMountSet(out, "rightArm", mountParams[i].rightArm);
        out << ",\n     ";
        copyFirstPersonArmMountSet(out, "leftArm", mountParams[i].leftArm);
        out << ",\n     .scale = " << mountParams[i].scale << "f},\n";
    }
    out << "}};\n";
    return out.str();
}

/// @brief Resolve a `ClientId` to its display nickname.
///
/// Reads the replicated `PlayerName` component (set server-side from the
/// `player_nicknames::k_nicknames` pool) and returns it.  When the
/// component hasn't replicated yet — or the entity simply doesn't exist
/// (e.g. a kill-feed entry referencing a player who already disconnected)
/// — falls back to a `Player #N` placeholder written into the caller's
/// `outBuf` so the returned `const char*` always has stable storage for
/// the lifetime of `outBuf`.
const char* lookupPlayerName(const Registry& registry, ClientId cid, char* outBuf, std::size_t bufSize)
{
    const auto v = registry.view<const ClientId>();
    for (auto entity : v) {
        if (v.get<const ClientId>(entity) == cid) {
            if (const auto* pn = registry.try_get<PlayerName>(entity); pn != nullptr && !pn->empty())
                return pn->c_str();
            break;
        }
    }
    SDL_snprintf(outBuf, bufSize, "Player #%d", cid.value);
    return outBuf;
}

std::string rosterEventPlayerName(const Registry& registry, const PlayerRosterEvent& event)
{
    // Prefer the server-snapshotted name because disconnects can remove the
    // replicated player entity before this client renders the notification.
    const auto* nameBegin = event.name;
    const auto* nameEnd = std::find(nameBegin, nameBegin + sizeof(event.name), '\0');
    if (nameEnd != nameBegin)
        return std::string(nameBegin, nameEnd);

    char nameBuf[32];
    return lookupPlayerName(registry, event.id, nameBuf, sizeof(nameBuf));
}

struct ClientRagdollBonePose
{
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    bool present = false;
};

using ClientRagdollPose = std::array<ClientRagdollBonePose, static_cast<size_t>(RagdollBone::Count)>;

struct RagdollJointBinding
{
    RagdollBone bone;
    const char* jointName;
};

constexpr RagdollJointBinding kRagdollJointBindings[] = {
    {RagdollBone::Pelvis, "mixamorig:Hips"},
    {RagdollBone::Torso, "mixamorig:Spine"},
    {RagdollBone::Torso, "mixamorig:Spine1"},
    {RagdollBone::Torso, "mixamorig:Spine2"},
    {RagdollBone::Head, "mixamorig:Neck"},
    {RagdollBone::Head, "mixamorig:Head"},
    {RagdollBone::UpperArmL, "mixamorig:LeftShoulder"},
    {RagdollBone::UpperArmL, "mixamorig:LeftArm"},
    {RagdollBone::ForearmL, "mixamorig:LeftForeArm"},
    {RagdollBone::HandL, "mixamorig:LeftHand"},
    {RagdollBone::UpperArmR, "mixamorig:RightShoulder"},
    {RagdollBone::UpperArmR, "mixamorig:RightArm"},
    {RagdollBone::ForearmR, "mixamorig:RightForeArm"},
    {RagdollBone::HandR, "mixamorig:RightHand"},
    {RagdollBone::UpperLegL, "mixamorig:LeftUpLeg"},
    {RagdollBone::LowerLegL, "mixamorig:LeftLeg"},
    {RagdollBone::FootL, "mixamorig:LeftFoot"},
    {RagdollBone::FootL, "mixamorig:LeftToeBase"},
    {RagdollBone::UpperLegR, "mixamorig:RightUpLeg"},
    {RagdollBone::LowerLegR, "mixamorig:RightLeg"},
    {RagdollBone::FootR, "mixamorig:RightFoot"},
    {RagdollBone::FootR, "mixamorig:RightToeBase"},
};

std::unordered_map<ClientId, ClientRagdollPose> collectClientRagdollPoses(Registry& registry)
{
    std::unordered_map<ClientId, ClientRagdollPose> poses;
    if constexpr (!kRagdollsEnabled)
        return poses; // ragdolls disabled — no corpse poses to collect/render
    registry.view<RagdollBoneTag, Position>().each([&](entt::entity e, const RagdollBoneTag& tag, const Position& pos) {
        if (tag.characterId.value < 0)
            return;

        const size_t boneIndex = static_cast<size_t>(tag.bone);
        if (boneIndex >= static_cast<size_t>(RagdollBone::Count))
            return;

        auto& bone = poses[tag.characterId][boneIndex];
        bone.position = pos.value;
        if (const auto* orientation = registry.try_get<Orientation>(e))
            bone.orientation = orientation->value;
        else
            bone.orientation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        bone.present = true;
    });
    return poses;
}

void applyRagdollPoseToSkinPalette(std::vector<glm::mat4>& skinMatrices,
                                   const CharacterRig& rig,
                                   const ClientRagdollPose& pose,
                                   const glm::mat4& instanceWorld,
                                   float rigScale)
{
    const auto& jointMap = rig.jointMap();
    const auto& inverseBind = rig.inverseBindMatrices();
    const glm::mat4 inverseInstanceWorld = glm::inverse(instanceWorld);

    // The mesh bind pose lives in MESH space (typically Mixamo ~160 units
    // tall), while the ragdoll physics bones are in WORLD space (player
    // capsule is 72 units tall). The skin matrix must scale vertex offsets
    // FROM mesh-space units TO world-space units before placing them at the
    // ragdoll bone, otherwise the rendered mesh appears at mesh scale and
    // looks ~2× larger than the live player capsule. Multiplying `boneWorld`
    // by `scale(rigScale)` does exactly that: inverseBind produces a
    // bind-local mesh-unit offset, scale() converts it to world units, then
    // the rotation + translation places it at the ragdoll bone.
    const glm::mat4 ragdollBoneScale = glm::scale(glm::mat4(1.0f), glm::vec3(rigScale));

    for (const RagdollJointBinding& binding : kRagdollJointBindings) {
        const size_t boneIndex = static_cast<size_t>(binding.bone);
        if (boneIndex >= pose.size() || !pose[boneIndex].present)
            continue;

        const auto jointIt = jointMap.find(binding.jointName);
        if (jointIt == jointMap.end())
            continue;

        const int jointIndex = jointIt->second;
        if (jointIndex < 0 || static_cast<size_t>(jointIndex) >= skinMatrices.size() ||
            static_cast<size_t>(jointIndex) >= inverseBind.size())
        {
            continue;
        }

        const ClientRagdollBonePose& bone = pose[boneIndex];
        const glm::mat4 boneWorld = glm::translate(glm::mat4(1.0f), bone.position) *
                                    glm::mat4_cast(glm::normalize(bone.orientation)) * ragdollBoneScale;
        const glm::mat4 modelSpaceBone = inverseInstanceWorld * boneWorld;
        skinMatrices[static_cast<size_t>(jointIndex)] = modelSpaceBone * inverseBind[static_cast<size_t>(jointIndex)];
    }
}

void popUtf8Codepoint(std::string& text)
{
    if (text.empty())
        return;
    std::size_t firstByte = text.size() - 1;
    while (firstByte > 0 && (static_cast<unsigned char>(text[firstByte]) & 0xc0u) == 0x80u)
        --firstByte;
    text.erase(firstByte);
}

void appendBoundedUtf8(std::string& dst, const char* text)
{
    if (!text || !*text)
        return;
    std::string candidate = dst;
    candidate += text;
    if (candidate.size() > net::chat::k_maxChatBytes)
        return;
    if (!net::chat::isValidUtf8(candidate))
        return;
    dst = std::move(candidate);
}

bool isFootstepClip(ClipId id) noexcept
{
    switch (id) {
    case ClipId::Walk:
    case ClipId::Run:
    case ClipId::RunBackward:
    case ClipId::SlowRun:
    case ClipId::WallRun:
    case ClipId::StrafeLeft:
    case ClipId::StrafeRight:
    case ClipId::StrafeLeftWalk:
    case ClipId::StrafeRightWalk:
    case ClipId::CrouchWalk:
    case ClipId::CrouchWalkLeft:
    case ClipId::CrouchWalkRight:
    case ClipId::CrouchWalkBackward:
        return true;
    default:
        return false;
    }
}

bool footstepMarkerCrossed(float previous, float current, float marker) noexcept
{
    if (previous < 0.0f)
        return false;
    if (current < previous)
        return marker > previous || marker <= current;
    return marker > previous && marker <= current;
}

bool isHeavyFootstepClip(ClipId id) noexcept
{
    return id == ClipId::Run || id == ClipId::RunBackward || id == ClipId::SlowRun || id == ClipId::WallRun ||
           id == ClipId::StrafeLeft || id == ClipId::StrafeRight;
}

std::string_view fireAudioEventForWeapon(WeaponType type) noexcept
{
    switch (type) {
    case WeaponType::Rifle:
        return "weapon.rifle.fire";
    case WeaponType::Rocket:
        return "weapon.rocket.fire";
    case WeaponType::RailGun:
        return "weapon.railgun.fire";
    case WeaponType::EnergyGun:
        return "weapon.energy.fire";
    case WeaponType::Shotgun:
        return "weapon.shotgun.fire"; // falls back gracefully if SFX bank lacks this event.
    case WeaponType::HEGrenade:
    case WeaponType::Molotov:
    case WeaponType::Sticky:
        return "weapon.grenade.throw";
    }
    return {};
}

audio::AudioObjectId audioObjectForEntity(entt::entity entity) noexcept
{
    const auto raw = static_cast<std::uint32_t>(entt::to_integral(entity));
    return audio::AudioObjectId{raw == 0 ? 1u : raw};
}

std::vector<RigMeshSource> buildRigMeshSources(const CharacterRig& rig)
{
    std::vector<RigMeshSource> sources;
    sources.reserve(rig.meshes().size());

    for (const RigMeshData& mesh : rig.meshes()) {
        RigMeshSource source;
        source.materialIndex = mesh.materialIndex;
        source.bindPoseVertices = mesh.baseVertices;
        source.indices = mesh.indices;
        source.boneInfluences.reserve(mesh.skinWeights.size());

        for (const SkinWeight& weight : mesh.skinWeights) {
            BoneInfluence influence;
            for (int i = 0; i < 4; ++i) {
                influence.boneIndices[i] = weight.boneIndices[i];
                influence.boneWeights[i] = weight.weights[i];
            }
            source.boneInfluences.push_back(influence);
        }

        sources.push_back(std::move(source));
    }

    return sources;
}

void centerMouseInWindow(SDL_Window* window)
{
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    SDL_WarpMouseInWindow(window, static_cast<float>(winW) * 0.5f, static_cast<float>(winH) * 0.5f);
}

float verticalFovRadiansFromHorizontal(float horizontalFovDegrees, float aspect)
{
    const float safeAspect = aspect > 0.0f ? aspect : 1.0f;
    const float horizontalRadians = glm::radians(horizontalFovDegrees);
    return 2.0f * std::atan(std::tan(horizontalRadians * 0.5f) / safeAspect);
}
} // namespace

bool Game::initDebugUI(const AppContext& ctx)
{
    window = &ctx.window;

    if (!debugUI.init(window)) {
        SDL_Log("DebugUI init failed");
        return false;
    }

    return true;
}

void Game::handleLocalPlayerReady(entt::entity local)
{
    registry.emplace<LocalPlayer>(local);
    registry.emplace<InputSnapshot>(local);
    registry.emplace_or_replace<PreviousPosition>(local, registry.get<Position>(local).value);
    registry.emplace_or_replace<PlayerSimState>(local);
    clearPredictedStateHistory();

    if (!registry.all_of<RespawnTimer>(local))
        registry.emplace<Controllable>(local);

    attachAnimatedCharacter(local);
    mappedLocalPlayerEntity_ = local;

    SDL_Log("[client] local player entity assigned: %d", static_cast<int>(local));
}

void Game::clearPredictedStateHistory() noexcept
{
    for (PredictedPlayerState& state : predictedStateHistory_)
        state.valid = false;
}

std::optional<Game::PredictedPlayerState> Game::captureLocalPredictedState() const
{
    if (!mappedLocalPlayerEntity_)
        return std::nullopt;
    const entt::entity local = *mappedLocalPlayerEntity_;
    if (!registry.valid(local))
        return std::nullopt;

    const auto* position = registry.try_get<Position>(local);
    const auto* previousPosition = registry.try_get<PreviousPosition>(local);
    const auto* velocity = registry.try_get<Velocity>(local);
    const auto* vis = registry.try_get<PlayerVisState>(local);
    const auto* sim = registry.try_get<PlayerSimState>(local);
    const auto* input = registry.try_get<InputSnapshot>(local);
    if (position == nullptr || previousPosition == nullptr || velocity == nullptr || vis == nullptr || sim == nullptr ||
        input == nullptr)
        return std::nullopt;

    PredictedPlayerState state;
    state.tick = input->tick;
    state.position = *position;
    state.previousPosition = *previousPosition;
    state.velocity = *velocity;
    state.vis = *vis;
    state.sim = *sim;
    state.input = *input;
    state.valid = true;
    return state;
}

void Game::storePredictedPlayerState(std::uint32_t tick)
{
    std::optional<PredictedPlayerState> state = captureLocalPredictedState();
    if (!state)
        return;
    state->tick = tick;
    const std::size_t idx = static_cast<std::size_t>(tick % InputRingBuffer::k_capacity);
    predictedStateHistory_[idx] = *state;
}

const Game::PredictedPlayerState* Game::predictedStateForTick(std::uint32_t tick) const noexcept
{
    const PredictedPlayerState& state =
        predictedStateHistory_[static_cast<std::size_t>(tick % InputRingBuffer::k_capacity)];
    if (!state.valid || state.tick != tick)
        return nullptr;
    return &state;
}

void Game::restoreLocalPredictedState(const PredictedPlayerState& state)
{
    if (!mappedLocalPlayerEntity_)
        return;
    const entt::entity local = *mappedLocalPlayerEntity_;
    if (!registry.valid(local))
        return;

    registry.emplace_or_replace<Position>(local, state.position);
    registry.emplace_or_replace<PreviousPosition>(local, state.previousPosition);
    registry.emplace_or_replace<Velocity>(local, state.velocity);
    registry.emplace_or_replace<PlayerVisState>(local, state.vis);
    registry.emplace_or_replace<PlayerSimState>(local, state.sim);
    registry.emplace_or_replace<InputSnapshot>(local, state.input);
}

Game::ReconciliationDecision
Game::evaluateReconciliationSkip(const PredictedPlayerState& authoritative,
                                 const PredictedPlayerState* predictedAtAck,
                                 const std::optional<PredictedPlayerState>& currentBeforeSnapshot) const noexcept
{
    ReconciliationDecision decision{};
    if (predictedAtAck == nullptr || !currentBeforeSnapshot) {
        decision.missingHistory = true;
        return decision;
    }

    decision.positionError = glm::length(authoritative.position.value - predictedAtAck->position.value);
    decision.velocityError = glm::length(authoritative.velocity.value - predictedAtAck->velocity.value);

    constexpr float kPositionTolerance = 0.35f;
    constexpr float kVelocityTolerance = 2.0f;
    constexpr float kNormalTolerance = 0.01f;
    constexpr float kPointTolerance = 0.5f;
    constexpr float kCameraTiltTolerance = 0.5f;

    const PlayerVisState& a = authoritative.vis;
    const PlayerVisState& p = predictedAtAck->vis;
    const bool visMatches = a.moveMode == p.moveMode && a.wallRunSide == p.wallRunSide && a.jumpCount == p.jumpCount &&
                            a.isDead == p.isDead && a.grounded == p.grounded && a.crouching == p.crouching &&
                            a.sprinting == p.sprinting && a.pendingUncrouch == p.pendingUncrouch &&
                            a.exitingWall == p.exitingWall && a.grappleActive == p.grappleActive &&
                            a.gravityFlipped == p.gravityFlipped &&
                            glm::length(a.groundNormal - p.groundNormal) <= kNormalTolerance &&
                            glm::length(a.grapplePoint - p.grapplePoint) <= kPointTolerance &&
                            std::abs(a.targetCameraTilt - p.targetCameraTilt) <= kCameraTiltTolerance;

    decision.skip =
        visMatches && decision.positionError <= kPositionTolerance && decision.velocityError <= kVelocityTolerance;
    return decision;
}

bool Game::applyIncomingSnapshot(
    std::uint32_t /*snapshotTick*/, const std::uint8_t* bytes, Uint32 size, Uint64 captureNs, std::uint32_t& ackedTick)
{
    const bool collectSnapshotPerf = benchActive_ || perfRecorder_.isRecording();
    const Uint64 perfStart = collectSnapshotPerf ? SDL_GetPerformanceCounter() : 0;
    registry.view<Position, PreviousPosition>(entt::exclude<LocalPlayer>)
        .each([](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

    if (!snapshotLoader_)
        snapshotLoader_.emplace(registry);

    snapshotLoader_->apply(bytes, size, client->getServerLocalPlayerEntity(), &ackedTick);

    registry.view<Position>(entt::exclude<PreviousPosition>).each([this](entt::entity e, const Position& pos) {
        registry.emplace<PreviousPosition>(e, pos.value);
    });

    if (!mappedLocalPlayerEntity_) {
        if (const auto serverLocal = client->getServerLocalPlayerEntity()) {
            const entt::entity mapped = snapshotLoader_->map(*serverLocal);
            if (mapped != entt::null)
                handleLocalPlayerReady(mapped);
        }
    }

    client->recordInterpolationSamples(registry, captureNs);
    if (collectSnapshotPerf) {
        const Uint64 freq = SDL_GetPerformanceFrequency();
        perfSnapshotApplyMs_ +=
            static_cast<float>(SDL_GetPerformanceCounter() - perfStart) * 1000.0f / static_cast<float>(freq);
        ++perfSnapshotApplyCount_;
    }
    return true;
}

bool Game::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    client = &ctx.client;
    userSettings = &ctx.userSettings;
    userSettingsPath_ = ctx.userSettingsPath;
    mouseSensitivity = userSettings->mouseSensitivity;
    horizontalFovDegrees = userSettings->horizontalFovDegrees;
    renderer->mainHorizontalFovDegrees = horizontalFovDegrees;

    if (const auto latestMatchState = client->getLatestMatchState()) {
        currentMatchPhase = latestMatchState->phase;
        countdownTimer = latestMatchState->countdownTimer;
    }
    client->resetInputHistory();

    // Bind any controller already plugged in before the match started. SDL only
    // fires SDL_EVENT_GAMEPAD_ADDED for those at SDL_Init time — long before this
    // screen exists — so without an explicit scan a pre-connected pad stays
    // unbound until the user physically reconnects it. Runtime hot-plug is still
    // handled by the GAMEPAD_ADDED/REMOVED events in Game::event().
    scanForConnectedGamepads();

    physics::diag::setFilePrefix("client");
    const char* phaseDiagEnv = std::getenv("GROUP2_PHASE_DIAG");
    const bool phaseDiagEnabled = phaseDiagEnv != nullptr && phaseDiagEnv[0] != '\0' && phaseDiagEnv[0] != '0';
    physics::diag::setEnabled(phaseDiagEnabled);
    SDL_Log("[client] physics diagnostic %s", phaseDiagEnabled ? "ENABLED" : "disabled");

    // Particle pipelines must match the active render-pass color format.
    // ParticleRenderer draws inside NewRenderer's scene color pass, so its
    // pipeline color format must match that HDR target.
    if (!particleSystem.init(renderer->getDevice(), NewRenderer::getHdrFormat(), renderer->getShaderFormat())) {
        SDL_Log("ParticleSystem init failed (non-fatal — particles disabled)");
    } else {
        renderer->setParticleSystem(&particleSystem);

        // Wire dispatcher events to particle system.
        // NOTE: WeaponFiredEvent is NOT wired here — local weapon VFX (tracers,
        // beams, impacts) are spawned explicitly in iterate() so we control
        // exactly which effect plays per weapon type.  onWeaponFired would
        // unconditionally spawn a hitscan beam for every hitscan weapon.
        dispatcher.sink<ProjectileImpactEvent>().connect<&ParticleSystem::onImpact>(particleSystem);
        dispatcher.sink<ExplosionEvent>().connect<&ParticleSystem::onExplosion>(particleSystem);
    }

    // Sound effects system — initialised after particles so audio can mirror the
    // same event-driven pattern.  Failure is non-fatal: the game runs silently.
    if (!sfxSystem.init()) {
        SDL_Log("[client] SfxSystem init failed (non-fatal — sound effects disabled)");
    } else {
        // WeaponFiredEvent: play the weapon fire sound for every shot.
        dispatcher.sink<WeaponFiredEvent>().connect<&SfxSystem::onWeaponFired>(sfxSystem);
        // ExplosionEvent: also play the explosion SFX alongside the particle effect.
        dispatcher.sink<ExplosionEvent>().connect<&SfxSystem::onExplosion>(sfxSystem);
    }
    // Phase F: same WeaponFiredEvent → push an additive pitch impulse onto
    // the shooter's spine via CharacterAnimator. Subscribes regardless of
    // SFX init result so recoil visuals still work in silent test runs.
    dispatcher.sink<WeaponFiredEvent>().connect<&Game::onWeaponFired>(*this);
    if (ctx.developerConfig.voiceCapture)
        voiceChat_.init();

    // HUD system — needs device + shader format from renderer, SDF atlas from particles.
    if (particleSystem.sdfReady()) {
        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        if (!hud_.init(renderer->getDevice(),
                       renderer->getShaderFormat(),
                       particleSystem.sdfAtlas(),
                       static_cast<uint32_t>(winW),
                       static_cast<uint32_t>(winH)))
        {
            SDL_Log("Hud init failed (non-fatal — HUD disabled)");
        } else {
            renderer->setHudTexture(hud_.getOutputTexture());
        }
    }

    // ── Load map ──────────────────────────────────────────────────────────
    // Scale: the map was authored in meters; the game uses Quake units (inches).
    // 1 m = 39.3701 in.
    //
    // Map filename and load-mode toggles live in ecs/MapConfig.hpp so the
    // client and server load the exact same primitives (a prerequisite for
    // prediction parity).  To switch maps, edit `kMapAsset` in AssetCatalog.hpp.
    // To change *how* the map is loaded, edit the constants in MapConfig.hpp.
    {
        // 1) Extract collision geometry (shared with server).
        gamemap::loadConfiguredMap(mapCollision_, "client");

        // 2) Load visual model for rendering (scene-pass so it draws as static world geometry).
        // In separated mode, exclude collision-only nodes so they aren't rendered.
        const std::string visualExclude =
            gamemap::k_separatedCollisionMap ? std::string(gamemap::k_collisionPattern) : std::string();
        const int mapId = addAssetDefinition(assets_, kMapAsset);
        const int mapModelIdx = renderer->loadSceneModel(
            kMapAsset.filename, kMapAsset.loadTranslation, kMapAsset.loadScale, kMapAsset.flipUVs, visualExclude);
        assets_.setModelIndex(mapId, mapModelIdx);
        if (mapModelIdx >= 0) {
            // TODO(renderer-migration): renderer->setModelScenePass(mapModelIdx, true);
            renderer->setModelScenePass(mapModelIdx, true);
            SDL_Log("[client] map visual loaded (model index %d, exclude='%s')", mapModelIdx, visualExclude.c_str());
        } else {
            SDL_Log("[client] WARNING: map visual load failed — map will be invisible");
        }
    }

    // Optional art-check GLB. Drop assets/test_model.glb into the copied assets
    // directory to see it 50 units above the authored rifle spawner.
    if (const char* testModelFilename = findExistingOptionalTestModelFilename(); testModelFilename != nullptr) {
        if (const std::optional<glm::vec3> testModelPos = findOptionalTestModelPosition(); testModelPos.has_value()) {
            const int id = assets_.add("test_model", testModelFilename, AssetRole::Prop);
            const int modelIdx =
                renderer->loadSceneModel(testModelFilename, *testModelPos, kOptionalTestModelLoadScale, false);
            assets_.setModelIndex(id, modelIdx);
            if (modelIdx >= 0) {
                renderer->setModelScenePass(modelIdx, true);
                SDL_Log("[client] test model '%s' loaded at (%.1f, %.1f, %.1f)",
                        testModelFilename,
                        static_cast<double>(testModelPos->x),
                        static_cast<double>(testModelPos->y),
                        static_cast<double>(testModelPos->z));
            } else {
                SDL_Log("[client] WARNING: test model '%s' failed to load", testModelFilename);
            }
        } else {
            SDL_Log("[client] test model '%s' present, but no rifle spawner was loaded", testModelFilename);
        }
    }

    // ── Load props (render + collision) ───────────────────────────────────
    // These are standalone GLB models placed at fixed world positions.
    // Both visual and collision are loaded so players/projectiles interact.
    {
        const char* base = SDL_GetBasePath();
        const std::string basePath = base ? base : "";

        // Helper: load a prop with render + collision in one call. Non-convex
        // props fall back to triMesh in normal builds. Legacy V-HACD only runs
        // when both the asset/config opt in and CMake enables GROUP2_ENABLE_VHACD.
        auto loadProp = [&](const AssetDefinition& def) {
            const int id = addAssetDefinition(assets_, def);
            const int modelIdx =
                renderer->loadSceneModel(def.filename, def.loadTranslation, def.loadScale, def.flipUVs);
            assets_.setModelIndex(id, modelIdx);
            if (modelIdx >= 0) {
                // TODO(renderer-migration): renderer->setModelScenePass(modelIdx, true);
                renderer->setModelScenePass(modelIdx, true);
            }

            // Load collision at the same position/scale.
            const std::string fullPath = basePath + "assets/" + def.filename;
            const bool decompose = def.decomposeCollision && gamemap::k_useVhacd;
            if (physics::loadPropCollision(fullPath, mapCollision_, def.loadTranslation, def.loadScale, decompose)) {
                assets_.setHasCollision(id);
            }
        };

        for (const AssetDefinition& def : kPropAssets)
            loadProp(def);

        // Update the active world with the new collision data (map + all props).
        physics::setActiveWorld(mapCollision_.geometry());
    }

    // Load all weapon models (per WeaponType)
    {
        for (std::size_t i = 0; i < kWeaponAssets.size(); ++i) {
            const AssetDefinition& def = kWeaponAssets[i];
            if (def.filename) {
                const int id = addAssetDefinition(assets_, def);
                weaponAssetIds_[i] = id;
                weaponModelIndices_[i] =
                    renderer->loadSceneModel(def.filename, def.loadTranslation, def.loadScale, def.flipUVs);
                assets_.setModelIndex(id, weaponModelIndices_[i]);
                if (weaponModelIndices_[i] >= 0)
                    renderer->setModelScenePass(weaponModelIndices_[i], false);
                else
                    SDL_Log("[client] WARNING: weapon model '%s' failed to load", def.filename);
            }
        }

        // Load Rocket Projectile
        {
            const int id = addAssetDefinition(assets_, kRocketProjectile);
            rocketProjectileModelIdx_ = renderer->loadSceneModel(kRocketProjectile.filename,
                                                                 kRocketProjectile.loadTranslation,
                                                                 kRocketProjectile.loadScale,
                                                                 kRocketProjectile.flipUVs);
            assets_.setModelIndex(id, rocketProjectileModelIdx_);

            if (rocketProjectileModelIdx_ >= 0)
                renderer->setModelScenePass(rocketProjectileModelIdx_, false);
            else
                SDL_Log("[client] WARNING: rocket projectile model '%s' failed to load", kRocketProjectile.filename);
        }

        {
            const int id = addAssetDefinition(assets_, kGrenadeModel);
            grenadeModelIdx_ = renderer->loadSceneModel(
                kGrenadeModel.filename, kGrenadeModel.loadTranslation, kGrenadeModel.loadScale, kGrenadeModel.flipUVs);
            assets_.setModelIndex(id, grenadeModelIdx_);

            if (grenadeModelIdx_ >= 0)
                renderer->setModelScenePass(grenadeModelIdx_, false);
            else
                SDL_Log("[client] WARNING: grenade model '%s' failed to load", kGrenadeModel.filename);
        }

        {
            const int id = addAssetDefinition(assets_, kHEGrenadeModel);
            heGrenadeModelIdx_ = renderer->loadSceneModel(kHEGrenadeModel.filename,
                                                          kHEGrenadeModel.loadTranslation,
                                                          kHEGrenadeModel.loadScale,
                                                          kHEGrenadeModel.flipUVs);
            assets_.setModelIndex(id, heGrenadeModelIdx_);

            if (heGrenadeModelIdx_ >= 0)
                renderer->setModelScenePass(heGrenadeModelIdx_, false);
            else
                SDL_Log("[client] WARNING: HE grenade model '%s' failed to load", kHEGrenadeModel.filename);
        }

        {
            const int id = addAssetDefinition(assets_, kStickyGrenadeModel);
            stickyGrenadeModelIdx_ = renderer->loadSceneModel(kStickyGrenadeModel.filename,
                                                              kStickyGrenadeModel.loadTranslation,
                                                              kStickyGrenadeModel.loadScale,
                                                              kStickyGrenadeModel.flipUVs);
            assets_.setModelIndex(id, stickyGrenadeModelIdx_);

            if (stickyGrenadeModelIdx_ >= 0)
                renderer->setModelScenePass(stickyGrenadeModelIdx_, false);
            else
                SDL_Log("[client] WARNING: sticky grenade model '%s' failed to load", kStickyGrenadeModel.filename);
        }

        {
            const int id = addAssetDefinition(assets_, kMolotovModel);
            molotovModelIdx_ = renderer->loadSceneModel(
                kMolotovModel.filename, kMolotovModel.loadTranslation, kMolotovModel.loadScale, kMolotovModel.flipUVs);
            assets_.setModelIndex(id, molotovModelIdx_);

            if (molotovModelIdx_ >= 0)
                renderer->setModelScenePass(molotovModelIdx_, false);
            else
                SDL_Log("[client] WARNING: molotov model '%s' failed to load", kMolotovModel.filename);
        }

        {
            const int id = addAssetDefinition(assets_, kMedkitModel);
            medkitModelIdx_ = renderer->loadSceneModel(
                kMedkitModel.filename, kMedkitModel.loadTranslation, kMedkitModel.loadScale, kMedkitModel.flipUVs);
            assets_.setModelIndex(id, medkitModelIdx_);

            if (medkitModelIdx_ < 0)
                SDL_Log("[client] WARNING: medkit model '%s' failed to load", kMedkitModel.filename);
        }

        viewmodelLeftHandModelIdx_ = renderer->loadSceneModel("viewmodel_hand_left.glb", glm::vec3{0.0f}, 1.0f, false);
        viewmodelRightHandModelIdx_ =
            renderer->loadSceneModel("viewmodel_hand_right.glb", glm::vec3{0.0f}, 1.0f, false);
        renderer->setModelScenePass(viewmodelLeftHandModelIdx_, false);
        renderer->setModelScenePass(viewmodelRightHandModelIdx_, false);
        if (viewmodelLeftHandModelIdx_ < 0 || viewmodelRightHandModelIdx_ < 0) {
            SDL_Log("[client] WARNING: viewmodel hand assets failed to load — first-person hands disabled");
        }

        handMountDebugMarkerModelIdx_ = renderer->loadSceneModel("debug_red_dot.glb", glm::vec3{0.0f}, 1.0f, false);
        renderer->setModelScenePass(handMountDebugMarkerModelIdx_, false);
        if (handMountDebugMarkerModelIdx_ < 0)
            SDL_Log("[client] WARNING: debug hand-mount marker asset failed to load");
    }

    // Log the full asset registry.
    SDL_Log("[client] Asset registry: %d entries", assets_.count());
    for (const auto& e : assets_.entries())
        SDL_Log("[client]   '%s' → model %d (role=%d, collision=%s)",
                e.name.c_str(),
                e.modelIndex,
                static_cast<int>(e.role),
                e.hasCollision ? "yes" : "no");

    // Remove Controllable when the local player dies (RespawnTimer added),
    // restore it when they respawn (RespawnTimer removed).
    registry.on_construct<RespawnTimer>().connect<[](entt::registry& reg, entt::entity e) {
        if (reg.all_of<LocalPlayer>(e))
            reg.remove<Controllable>(e);
    }>();
    registry.on_destroy<RespawnTimer>().connect<[](entt::registry& reg, entt::entity e) {
        if (reg.all_of<LocalPlayer>(e))
            reg.emplace_or_replace<Controllable>(e);
    }>();

    client->onSnapshotApply([this](std::uint32_t snapshotTick,
                                   const std::uint8_t* bytes,
                                   Uint32 size,
                                   Uint64 captureNs,
                                   std::uint32_t& ackedTick) {
        return applyIncomingSnapshot(snapshotTick, bytes, size, captureNs, ackedTick);
    });

    client->onRawParticleEvent([this](const NetParticleEvent& rawEvt) {
        if (!snapshotLoader_ || !mappedLocalPlayerEntity_)
            return;

        NetParticleEvent evt = rawEvt;
        evt.source = snapshotLoader_->map(evt.source);
        if (evt.target != entt::null)
            evt.target = snapshotLoader_->map(evt.target);
        const entt::entity localPlayer = *mappedLocalPlayerEntity_;

        // Hitmarker SFX: local player's shot was confirmed by the server to have
        // hit an enemy (surface == Flesh).  This check runs BEFORE the skip-self
        // guard so the shooter still hears the hitmarker even though their own
        // particle VFX was already spawned client-side for instant feedback.
        if (evt.source == localPlayer && evt.effectType == ParticleEffectType::Impact &&
            evt.surfaceType == SurfaceType::Flesh)
        {
            if (sfxSystem.isInitialized())
                sfxSystem.postAudioEvent("impact.flesh");
            hitmarkerTimer_ = 0.25f; // show hitmarker for 250ms
            hitmarkerIsHeadshot_ = (evt.headshot != 0);
            hitmarkerShieldBreak_ = (evt.shieldBreak != 0);

            // Queue floating damage number at hit position.
            if (evt.damage > 0.f) {
                const bool isHeadshot = (evt.headshot != 0);
                const bool isShielded = (evt.hadArmor != 0);
                pendingDamageNumbers_.push_back({evt.pos1, evt.damage, isHeadshot, isShielded});

                // Damage accumulator: reset if target changed, accumulate otherwise.
                if (evt.target != accumTarget_) {
                    accumTarget_ = evt.target;
                    accumTotal_ = 0;
                }
                accumTotal_ += static_cast<int>(evt.damage + 0.5f);
                accumResetTimer_ = 2.0f; // reset after 2s of no hits
                // Track latest hit type for accumulator color.
                accumLastHitType_ = isHeadshot ? uint8_t{2} : (isShielded ? uint8_t{1} : uint8_t{0});
            }
        }

        // Shotgun pellet readout: aggregate the 9 per-pellet impact events emitted
        // by the server into one HUD blast. Pellets arrive in server-emit order
        // within a single server tick → one client frame, so a simple index counter
        // matches the server's k_offsets star pattern. If a stale partial blast
        // exists (e.g., packet loss dropped a pellet), the time-gap reset starts
        // a fresh accumulator on the next shot.
        if (evt.source == localPlayer && evt.weaponType == WeaponType::Shotgun &&
            evt.effectType == ParticleEffectType::Impact)
        {
            const float nowSec = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            const bool stale = (shotgunPelletAccumCount_ > 0) && (nowSec - shotgunPelletLastTimeSec_) > 0.25f;
            if (shotgunPelletAccumCount_ == 0 || stale) {
                shotgunPelletAccum_ = HudShotgunBlast{};
                shotgunPelletAccumCount_ = 0;
            }
            if (shotgunPelletAccumCount_ < static_cast<int>(shotgunPelletAccum_.pellets.size())) {
                const bool isHit = (evt.surfaceType == SurfaceType::Flesh);
                const bool isHead = (evt.headshot != 0);
                shotgunPelletAccum_.pellets[shotgunPelletAccumCount_].result = isHead  ? HudShotgunPellet::Result::Head
                                                                               : isHit ? HudShotgunPellet::Result::Body
                                                                                       : HudShotgunPellet::Result::Miss;
                ++shotgunPelletAccumCount_;
                shotgunPelletLastTimeSec_ = nowSec;
                if (shotgunPelletAccumCount_ == static_cast<int>(shotgunPelletAccum_.pellets.size())) {
                    shotgunPelletAccum_.valid = true;
                    shotgunPelletAccum_.secondsSinceFire = 0.0f;
                    lastShotgunBlast_ = shotgunPelletAccum_;
                    shotgunPelletAccumCount_ = 0;
                }
            }
        }

        // Skip own effects that were already spawned locally for instant feedback.
        // Exceptions that must NOT be skipped:
        //   - Charge weapons: local VFX is skipped; we rely on server events.
        //   - Explosions / Smoke: server-authoritative (not locally predicted).
        //   - Impacts: local prediction only spawns tracers; all impact VFX
        //     (sparks, blood, bullet holes) come from server so player hits
        //     always get the correct surface type and normal.
        if (evt.source == localPlayer) {
            const bool isChargeWeapon = getWeaponConfig(evt.weaponType).isCharge;
            const bool isServerAuthoritative = evt.effectType == ParticleEffectType::Explosion ||
                                               evt.effectType == ParticleEffectType::Smoke ||
                                               evt.effectType == ParticleEffectType::Impact;
            if (!isChargeWeapon && !isServerAuthoritative)
                return;

            // Non-predicted local beam-style events still go through the
            // dispatcher. Charge-rifle fire audio is predicted locally at input
            // time so ADS shots are not delayed or swallowed by replication.
            if (evt.effectType == ParticleEffectType::HitscanBeam && !isChargeWeapon) {
                WeaponFiredEvent wfe;
                wfe.shooter = localPlayer;
                wfe.type = evt.weaponType;
                wfe.origin = evt.pos1;
                wfe.direction = glm::normalize(evt.pos2 - evt.pos1);
                wfe.isHitscan = true;
                wfe.localPlayer = true;
                wfe.hitPos = evt.pos2;
                dispatcher.enqueue(wfe);
            }
        }

        // For local player's charge weapon: override beam origin with
        // viewmodel muzzle position so the lightning comes from the gun.
        glm::vec3 evtOrigin = evt.pos1;
        if (evt.source == localPlayer && evt.effectType == ParticleEffectType::HitscanBeam) {
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const float cs = cachedGravFlipped_ ? -1.0f : 1.0f;
            evtOrigin = cachedEye_ + right * (cs * 15.f) - glm::vec3{0, 1, 0} * (cs * 8.f) + cachedCamFwd_ * 5.f;
            if (cachedMuzzleValid_) {
                evtOrigin = cachedMuzzleWorld_;
            }
        }

        // Pop a brief point light at the muzzle on every shot so the flash
        // lights up nearby geometry. For the local player prefer the exact
        // viewmodel muzzle; for everyone else use the shot's muzzle origin.
        if (evt.effectType == ParticleEffectType::BulletTracer || evt.effectType == ParticleEffectType::HitscanBeam) {
            // Local player: 10 units ahead of the right palm along the view dir
            // (muzzleFlashOrigin). Remote players: the shot's muzzle origin.
            const glm::vec3 flashPos = (evt.source == localPlayer) ? muzzleFlashOrigin(evtOrigin) : evtOrigin;
            spawnMuzzleFlashLight(flashPos);
        }

        switch (evt.effectType) {
        case ParticleEffectType::BulletTracer:
            particleSystem.spawnBulletTracer(evtOrigin, evt.pos2, evt.param);
            if (sfxSystem.isInitialized()) {
                const std::string_view eventName = fireAudioEventForWeapon(evt.weaponType);
                if (!eventName.empty()) {
                    const audio::AudioObjectId object = audioObjectForEntity(evt.source);
                    sfxSystem.setAudioObjectTransform(object, evtOrigin);
                    if (evt.source == localPlayer)
                        sfxSystem.postLocalAudioEvent(eventName, object, 0.82f);
                    else
                        sfxSystem.postAudioEvent(eventName, object, 0.82f);
                }
            }
            break;
        case ParticleEffectType::HitscanBeam:
            particleSystem.spawnHitscanBeam(evtOrigin, evt.pos2, evt.weaponType);
            if (sfxSystem.isInitialized() && !(evt.source == localPlayer && getWeaponConfig(evt.weaponType).isCharge)) {
                const std::string_view eventName = fireAudioEventForWeapon(evt.weaponType);
                if (!eventName.empty()) {
                    const audio::AudioObjectId object = audioObjectForEntity(evt.source);
                    sfxSystem.setAudioObjectTransform(object, evtOrigin);
                    if (evt.source == localPlayer)
                        sfxSystem.postLocalAudioEvent(eventName, object, 0.92f);
                    else
                        sfxSystem.postAudioEvent(eventName, object, 0.92f);
                }
            }
            break;
        case ParticleEffectType::Impact:
            particleSystem.spawnImpactEffect(evt.pos1, evt.pos2, evt.surfaceType, evt.weaponType);
            if (sfxSystem.isInitialized() && evt.source != localPlayer) {
                const audio::AudioObjectId object = audioObjectForEntity(evt.source);
                sfxSystem.setAudioObjectTransform(object, evt.pos1);
                sfxSystem.postAudioEvent(evt.surfaceType == SurfaceType::Flesh ? "impact.flesh" : "impact.world",
                                         object,
                                         evt.surfaceType == SurfaceType::Flesh ? 0.65f : 0.32f);
            }
            break;
        case ParticleEffectType::Explosion:
            particleSystem.spawnExplosionVfx(evt.pos1,
                                             glm::length(evt.pos2) > 0.001f ? glm::normalize(evt.pos2)
                                                                            : glm::vec3{0.0f, 1.0f, 0.0f},
                                             evt.param,
                                             explosionVfxKindForWeapon(evt.weaponType));
            spawnExplosionFlashLight(evt.pos1, evt.weaponType, evt.param);
            // Dispatch ExplosionEvent so SfxSystem plays the explosion sound.
            {
                ExplosionEvent expl;
                expl.pos = evt.pos1;
                expl.normal = glm::length(evt.pos2) > 0.001f ? glm::normalize(evt.pos2) : glm::vec3{0.0f, 1.0f, 0.0f};
                expl.blastRadius = evt.param;
                expl.weaponType = evt.weaponType;
                dispatcher.enqueue(expl);
            }
            break;
        case ParticleEffectType::Smoke:
            particleSystem.spawnSmoke(evt.pos1, evt.param);
            break;
        case ParticleEffectType::Fire:
            particleSystem.spawnExplosionVfx(evt.pos1, {0.0f, 1.0f, 0.0f}, evt.param, ExplosionVfxKind::Molotov);
            spawnExplosionFlashLight(evt.pos1, WeaponType::Molotov, evt.param);
            break;
        }
    });

    client->onMatchStateUpdate([this](const MatchStatePacket& packet) {
        currentWinnerId = ClientId{packet.winnerId};
        currentMatchPhase = packet.phase;
        countdownTimer = packet.countdownTimer;
        if (packet.phase == MatchPhase::LOBBY)
            returnToLobbyRequested = true;
    });

    client->onKillEvent([this](const NetKillEvent& evt) {
        killFeed.insert(killFeed.begin(),
                        KillFeedEvent{
                            evt.killerId,
                            evt.victimId,
                        });

        // TODO: Specific handling for local player deaths (display enemy health)
    });

    client->onTextChat([this](const net::chat::ServerTextChat& chat) { appendChatMessage(chat.sender, chat.message); });
    client->onRosterEvent([this](const PlayerRosterEvent& event) {
        const std::string playerName = rosterEventPlayerName(registry, event);
        switch (event.type) {
        case RosterEventType::PlayerJoined:
            appendPopupMessage(HudPopupKind::PlayerJoined, playerName + " JOINED THE MATCH");
            break;
        case RosterEventType::PlayerLeft:
            appendPopupMessage(HudPopupKind::PlayerLeft, playerName + " LEFT THE MATCH");
            break;
        }
    });
    client->onVoiceFrame([this](const net::voice::ServerVoiceFrame& frame) { voiceChat_.enqueueFrame(frame); });

    // PR-20: hand each SHOT_DEBUG_REPORT off to the DebugUI's ring
    // buffer.  Pairs with the client-side fire-time snapshot the
    // game thread captures inside iterate() (see fire-detection
    // block).  Always-on so the user can flip the overlay any time.
    client->onShotDebugReport([this](const net::shotdebug::ShotDebugCapture& cap) { debugUI.pushServerShot(cap); });

    // Initialize runtime 3P weapon params from defaults
    for (std::size_t i = 0; i < kRenderableWeaponTypeCount; ++i) {
        tpWeaponParams_[i] = getThirdPersonWeaponParams(static_cast<WeaponType>(i));
        weaponHoldPoses_[i] = getWeaponHoldPose(static_cast<WeaponType>(i));
        authoredWeaponHoldPoses_[i] = weaponHoldPoses_[i];
        authoredFPHandMountParams_[i] = getFirstPersonHandMountParams(static_cast<WeaponType>(i));
        makeFingerOffsetsPalmRelative(authoredFPHandMountParams_[i]);
        applyAuthoredFirstPersonHandMountDefaults(
            modelFromRendererIndex(weaponModelIndices_[static_cast<std::size_t>(i)]),
            getViewmodelParams(static_cast<WeaponType>(i)),
            authoredFPHandMountParams_[i]);
        fpHandMountParams_[i] = authoredFPHandMountParams_[i];
    }
    for (std::size_t i = 0; i < kWeaponAssets.size(); ++i)
        spawnerWeaponParams_[i] = defaultSpawnerModelParams(static_cast<WeaponType>(i));

    // Grab the mouse into relative mode so camera look works immediately.
    // Goes through the shared helper so a fresh Game instance entering after a
    // prior match cannot inherit stale text-input mode or queued mouse delta.
    input_capture::acquireGameplayInputCapture(window);
    mouseCaptured = true;
    chatOpen_ = false;

    // Animated first-person viewmodels, PER WEAPON (see kWeaponViewmodelAssets).
    // Load each weapon's gun + arms rig data plus hidden static models that
    // register their embedded textures. The renderer has a single viewmodel rig
    // slot, so the active weapon's rig is installed on equip (installed below in
    // iterate()). A weapon whose GLB is missing keeps weaponVmLoaded_[t]=false
    // and uses the legacy static viewmodel fallback path.
    {
        const char* base = SDL_GetBasePath();
        const std::string assetsBase = std::string(base ? base : "") + "assets/";
        weaponVmModelIdx_.fill(-1);
        weaponVmArmsModelIdx_.fill(-1);
        for (std::size_t t = 0; t < kWeaponViewmodelAssets.size(); ++t) {
            const WeaponViewmodelAssets& vma = kWeaponViewmodelAssets[t];
            if (!vma.viewmodelGlb || vma.viewmodelGlb[0] == '\0')
                continue;
            if (weaponVms_[t].load(assetsBase + vma.viewmodelGlb, vma.flipUVs)) {
                weaponVmLoaded_[t] = true;
                // Hidden static gun model: registers the gun GLB's embedded
                // materials/textures so the skinned viewmodel can bind them per-mesh.
                weaponVmModelIdx_[t] = renderer->loadSceneModel(vma.viewmodelGlb, glm::vec3{0.0f}, 1.0f, vma.flipUVs);
                if (weaponVmModelIdx_[t] >= 0)
                    renderer->setModelScenePass(weaponVmModelIdx_[t], false);
                // First-person arms (same baked clips) + their textures.
                if (vma.armsGlb && vma.armsGlb[0] != '\0') {
                    if (weaponVmArms_[t].load(assetsBase + vma.armsGlb, vma.flipUVs)) {
                        weaponVmArmsLoaded_[t] = true;
                        weaponVmArmsModelIdx_[t] =
                            renderer->loadSceneModel(vma.armsGlb, glm::vec3{0.0f}, 1.0f, vma.flipUVs);
                        if (weaponVmArmsModelIdx_[t] >= 0)
                            renderer->setModelScenePass(weaponVmArmsModelIdx_[t], false);
                    }
                }
            } else {
                SDL_Log("[client] WARNING: viewmodel '%s' failed to load — weapon %zu uses static fallback",
                        vma.viewmodelGlb,
                        t);
            }
            SDL_Log("[viewmodel] type=%zu glb='%s' loaded=%d arms=%d",
                    t,
                    vma.viewmodelGlb,
                    static_cast<int>(weaponVmLoaded_[t]),
                    static_cast<int>(weaponVmArmsLoaded_[t]));
        }
        // Spent-casing prop (hidden static model; spawned + drawn via the entity list on fire).
        shellEjectModelIdx_ = renderer->loadSceneModel("shelleject_assault_rifle.glb", glm::vec3{0.0f}, 1.0f, true);
        if (shellEjectModelIdx_ >= 0)
            renderer->setModelScenePass(shellEjectModelIdx_, false);
    }

    // Load the shared skinned-character rig (skeleton + bind pose + weights).
    // character_rigged_new.glb supplies the visible character mesh; animation
    // clips are layered on top via AnimationLibrary (same Mixamo skeleton).
    //
    // The .glb is a Blender glTF re-export (the .fbx had per-mesh bind-matrix
    // divergence that exploded the skin). That export baked the armature's
    // unapplied +90° X rotation into the rig, leaving it face-down in-engine,
    // and inverted normal handedness vs the old FBX. Correct both at load:
    // rotate the skeleton root -90° about X and flip normals.
    {
        const char* base = SDL_GetBasePath();
        const std::string assetsDir = std::string(base ? base : "") + "assets/animations/";
        const std::string rigPath = assetsDir + "character_rigged_new.glb";
        const glm::quat rigOrientationFix = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        if (!charRig_.loadFromFBX(rigPath, rigOrientationFix, /*flipNormals=*/true)) {
            SDL_Log("[client] WARNING: rig load failed — animated characters disabled");
        } else {
            SDL_Log("[client] rig loaded — %d joints, %zu mesh(es)", charRig_.numJoints(), charRig_.meshes().size());

            if (!renderer->skinned().rigInstalled()) {
                const std::vector<RigMeshSource> rigMeshes = buildRigMeshSources(charRig_);
                if (!renderer->setRig(rigMeshes, charRig_.numJoints())) {
                    SDL_Log("[client] WARNING: renderer rejected skinned rig — remote player bodies may be invisible");
                }
            }

            // Auto-calculate rig scale so the animated model matches the
            // player's standing hitbox height, and compute the vertical
            // offset so the model's feet sit at the bottom of the AABB.
            {
                float meshMinY = 0.0f;
                float meshMaxY = 1.0f;
                charRig_.verticalBounds(meshMinY, meshMaxY);
                rigMeshMinY_ = meshMinY;

                const float meshHeight = meshMaxY - meshMinY;
                const float targetHeight = 2.0f * tms::k_standingHalfHeight; // 72 units
                if (meshHeight > 0.001f) {
                    kRigScale_ = targetHeight / meshHeight;
                } else {
                    kRigScale_ = 1.0f;
                }
                // Offset: move the model so its feet (meshMinY) align with
                // the bottom of the standing AABB (pos.y - standingHalfHeight).
                // Translation is relative to the entity's Position (AABB centre),
                // so: translation.y = -halfHeight - meshMinY * scale.
                kRigVerticalOffset_ = -tms::k_standingHalfHeight - rigMeshMinY_ * kRigScale_;
                SDL_Log("[client] rig auto-scale: meshY=[%.1f, %.1f] height=%.1f -> scale=%.4f, vertOffset=%.1f",
                        static_cast<double>(meshMinY),
                        static_cast<double>(meshMaxY),
                        static_cast<double>(meshHeight),
                        static_cast<double>(kRigScale_),
                        static_cast<double>(kRigVerticalOffset_));
            }

            // Load every animation clip onto the shared skeleton. The rig is a
            // standard Mixamo skeleton matching the clips, so use the clips'
            // full translations (only scaled to the rig's units by clipScale in
            // loadClipFromFBX). This preserves authored hip/root vertical motion
            // so crouch/slide lower the body and the feet stay on the floor.
            for (uint8_t i = 0; i < static_cast<uint8_t>(ClipId::_Count); ++i) {
                const ClipId id = static_cast<ClipId>(i);
                const std::string clipPath = assetsDir + clipFile(id);
                if (!animLibrary_.loadClipFromFBX(charRig_, id, clipPath)) {
                    SDL_Log("[client] WARNING: failed to load clip '%s'", clipName(id));
                    continue;
                }
                SDL_Log(
                    "[client] clip '%s' duration=%.2fs", clipName(id), static_cast<double>(animLibrary_.duration(id)));
            }

            // Ground off the IDLE pose, not the T-pose bind. The clips bend the
            // knees slightly, so the animated feet sit a constant amount above
            // the straight-legged bind — grounding off the bind lifts every clip
            // by that amount. Re-reference the grounding min-Y to the idle feet
            // (now that clips are loaded) and recompute the vertical offset.
            {
                const float groundedMinY =
                    computeIdleGroundedMinY(charRig_, animLibrary_, rigOrientationFix, rigMeshMinY_);
                if (groundedMinY != rigMeshMinY_) {
                    SDL_Log("[client] rig grounding: idle-referenced minY %.3f -> %.3f",
                            static_cast<double>(rigMeshMinY_),
                            static_cast<double>(groundedMinY));
                    rigMeshMinY_ = groundedMinY;
                    kRigVerticalOffset_ = -tms::k_standingHalfHeight - rigMeshMinY_ * kRigScale_;
                }
            }

            // Cache the right-hand bone index. The third-person weapon mesh
            // is parented to this bone after IK (AAA pattern: weapon follows
            // hand, not vice versa), so we look it up once at rig load and
            // reuse the index every frame in the candidate writeback loop.
            if (auto it = charRig_.jointMap().find("mixamorig:RightHand"); it != charRig_.jointMap().end()) {
                rightHandJointIdx_ = it->second;
                SDL_Log("[client] right-hand bone index = %d", rightHandJointIdx_);
            } else {
                SDL_Log("[client] WARNING: right-hand bone not found — weapon parenting disabled");
            }
            if (auto it = charRig_.jointMap().find("mixamorig:Spine2"); it != charRig_.jointMap().end()) {
                spine2JointIdx_ = it->second;
                SDL_Log("[client] Spine2 bone index = %d", spine2JointIdx_);
            } else {
                SDL_Log("[client] WARNING: Spine2 bone not found — chest-anchored right-hand IK disabled");
            }

            // Load per-weapon third-person FK hold poses. Each weapon's pose
            // lives at assets/weapons/<name>.hold.toml. Missing files are
            // non-fatal — the compile-time default from getWeaponHoldPose
            // (already loaded above) is kept for any weapon without a TOML.
            const std::string weaponsDir = std::string(base ? base : "") + "assets/weapons/";
            static constexpr std::array<const char*, kRenderableWeaponTypeCount> k_weaponHoldFiles{
                "rifle.hold.toml",
                "rocket_launcher.hold.toml",
                "rail_gun.hold.toml",
                "energy_gun.hold.toml",
                "energy_gun.hold.toml",
            };
            for (std::size_t i = 0; i < k_weaponHoldFiles.size(); ++i) {
                const std::string holdPath = weaponsDir + k_weaponHoldFiles[i];
                weaponHoldPosePaths_[i] = holdPath;
                // Load on top of the compile-time default; on parse failure the
                // default (already in weaponHoldPoses_[i]) is left intact.
                loadWeaponHoldPose(holdPath, weaponHoldPoses_[i]);
                // Capture the file mtime for hot-reload. Stat failures (missing
                // file) leave a default-constructed time_point so the first
                // successful save triggers a reload.
                std::error_code ec;
                const auto mtime = std::filesystem::last_write_time(holdPath, ec);
                if (!ec)
                    weaponHoldPoseMTimes_[i] = mtime;
            }
        }

        // Build and resolve hitbox definitions (client-side, for debug visualization).
        clientHitboxRig_ = HitboxRig::buildMixamoDefault();
        clientHitboxRig_.resolveIndices(charRig_.jointMap());
        {
            int resolved = 0;
            for (const auto& def : clientHitboxRig_.definitions)
                if (def.boneIndex >= 0)
                    ++resolved;
            SDL_Log("[client] hitbox rig: %zu definitions, %d resolved", clientHitboxRig_.definitions.size(), resolved);
        }
    }

    prevTime = SDL_GetPerformanceCounter();
    statsPrevTime = prevTime;
    {
        const char* base = SDL_GetBasePath();
        perfRecorder_.configureFromEnv(base);
        perfRecorder_.start();
    }

    // Spin up the per-frame worker pool.  Default to half the host's logical
    // cores so we leave headroom for the rest of the system; clamp to [0, 7]
    // because parallel-for over ~30-character batches saturates well before
    // 8 workers and over-subscribing just adds context-switch noise.
    {
        int hw = static_cast<int>(std::thread::hardware_concurrency());
        if (hw <= 0)
            hw = 4;
        int workers = std::max(0, std::min(hw / 2, 7));
        if (const char* p = SDL_getenv("GROUP2_WORKERS")) {
            char* end = nullptr;
            const long n = std::strtol(p, &end, 10);
            if (*end == '\0' && n >= 0 && n <= 32)
                workers = static_cast<int>(n);
        }
        workerPool_ = std::make_unique<WorkerPool>(workers);
        SDL_Log("[client] WorkerPool: %d worker(s)", workers);
    }

    if (const char* uncapped = SDL_getenv("GROUP2_CLIENT_UNCAPPED");
        uncapped != nullptr && uncapped[0] != '\0' && uncapped[0] != '0')
    {
        limitFPSToMonitor = false;
        SDL_Log("[client] frame limiter disabled by GROUP2_CLIENT_UNCAPPED=1");
    }

    // Apply the default frame-rate-limit setting now that the renderer is ready.
    applyFrameRateLimit();

    // Bench mode: BENCH_SECONDS=N runs the client for N seconds, then prints a
    // single-line FPS summary to stderr and quits.  Driven by
    // `scripts/perf-100bots.sh` for baseline + post-change measurements.
    if (const char* envBench = SDL_getenv("BENCH_SECONDS")) {
        const float seconds = std::strtof(envBench, nullptr);
        if (seconds > 0.0f) {
            benchSeconds_ = seconds;
            benchActive_ = true;
            benchStartTime_ = prevTime;
            // Reserve enough room for ~5000 fps × bench duration (worst case),
            // so push_back never reallocates inside the hot path.
            benchFrameTimesMs_.reserve(static_cast<size_t>(seconds * 5000.0f));
            benchFrameStats_.reserve(static_cast<size_t>(seconds * 5000.0f));
            // Drop the renderer's vsync limiter so the bench reflects raw client capacity.
            limitFPSToMonitor = false;
            applyFrameRateLimit();
            SDL_Log("[bench] running for %.1fs then exiting (warmup %.1fs)",
                    static_cast<double>(seconds),
                    static_cast<double>(k_benchWarmupSeconds));
        }
    }

    // BENCH_RENDER_SCALE=N — internal HDR + post-process resolution multiplier.
    // 0.5 = quarter pixel count = ~4× fragment savings.  Tonemap reads HDR via
    // linear sampler so the final swapchain image is bilinearly upscaled.

    // GROUP2_NO_IMGUI=1 — release-build kill switch.  Skips ImGui submission
    // unconditionally.  The CMake/release pipeline can pre-set this for
    // shipping builds that never need a debug menu.  Independent of bench
    // mode so it can be combined with normal play.
    if (const char* noImgui = SDL_getenv("GROUP2_NO_IMGUI");
        noImgui != nullptr && noImgui[0] != '\0' && noImgui[0] != '0')
    {
        renderer->imguiEnabled = false;
        SDL_Log("[client] ImGui GPU submission disabled by GROUP2_NO_IMGUI=1");
    }

    // GROUP2_CLIENT_CORES="0,1,2,3" — pin the main render thread to the
    // listed CPU cores via pthread_setaffinity_np.  Stops the OS scheduler
    // from migrating us onto a core where the colocated server / 100 bot
    // threads are spinning, which is the dominant cause of p1/p5 stalls
    // we observed in the phase profiler (acquire/record/submit phases
    // randomly bloating to several ms).  Linux only.
#if defined(__linux__)
    if (const char* corestr = SDL_getenv("GROUP2_CLIENT_CORES")) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        const char* p = corestr;
        int coreCount = 0;
        while (*p) {
            char* end = nullptr;
            const long c = std::strtol(p, &end, 10);
            if (end == p)
                break;
            if (c >= 0 && c < CPU_SETSIZE) {
                CPU_SET(static_cast<int>(c), &cpuset);
                ++coreCount;
            }
            p = end;
            if (*p == ',')
                ++p;
        }
        if (coreCount > 0) {
            const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
            if (rc == 0) {
                SDL_Log("[client] pinned render thread to %d core(s) (%s)", coreCount, corestr);
            } else {
                SDL_Log("[client] pthread_setaffinity_np failed: %s", std::strerror(rc));
            }
        }
    }
#endif

    SDL_Log("[client] local player spawned at (0, 200, 0), physicsHz=%d", k_physicsHz);

    client->sendGameplayReady();
    return true;
}

void Game::openChat()
{
    if (chatOpen_)
        return;
    chatOpen_ = true;
    chatDraft_.clear();
    clearGameplayInputForChat();
    SDL_StartTextInput(window);
}

void Game::closeChat()
{
    if (!chatOpen_)
        return;
    chatOpen_ = false;
    chatDraft_.clear();
    SDL_StopTextInput(window);
    clearGameplayInputForChat();
}

void Game::submitChat()
{
    const std::string clean = net::chat::sanitizeUtf8(chatDraft_);
    if (!clean.empty() && client->sendChatMessage(clean)) {
        appendLocalChatMessage(clean);
        pendingLocalChatEchoes_.push_back(clean);
        constexpr std::size_t k_maxPendingLocalEchoes = 8;
        if (pendingLocalChatEchoes_.size() > k_maxPendingLocalEchoes)
            pendingLocalChatEchoes_.pop_front();
    }
    closeChat();
}

void Game::appendChatMessage(ClientId sender, std::string_view message)
{
    char nameBuf[32];
    ClientId localClientId{-1};
    registry.view<LocalPlayer, ClientId>().each([&](const ClientId& cid) { localClientId = cid; });

    const bool fromLocal = localClientId.value != -1 && sender == localClientId;
    if (fromLocal) {
        const auto pendingIt = std::find(pendingLocalChatEchoes_.begin(), pendingLocalChatEchoes_.end(), message);
        if (pendingIt != pendingLocalChatEchoes_.end()) {
            pendingLocalChatEchoes_.erase(pendingIt);
            return;
        }
    }

    HudChatMessage entry;
    entry.fromLocal = fromLocal;
    entry.senderName = entry.fromLocal ? "You" : lookupPlayerName(registry, sender, nameBuf, sizeof(nameBuf));
    entry.message = std::string(message);
    entry.ageSeconds = 0.0f;
    chatMessages_.push_back(std::move(entry));
    constexpr std::size_t k_maxChatHistory = 64;
    if (chatMessages_.size() > k_maxChatHistory)
        chatMessages_.erase(chatMessages_.begin(),
                            chatMessages_.begin() +
                                static_cast<std::ptrdiff_t>(chatMessages_.size() - k_maxChatHistory));
}

void Game::appendLocalChatMessage(std::string_view message)
{
    HudChatMessage entry;
    entry.fromLocal = true;
    entry.senderName = "You";
    entry.message = std::string(message);
    entry.ageSeconds = 0.0f;
    chatMessages_.push_back(std::move(entry));
    constexpr std::size_t k_maxChatHistory = 64;
    if (chatMessages_.size() > k_maxChatHistory)
        chatMessages_.erase(chatMessages_.begin(),
                            chatMessages_.begin() +
                                static_cast<std::ptrdiff_t>(chatMessages_.size() - k_maxChatHistory));
}

void Game::appendPopupMessage(HudPopupKind kind, std::string_view message)
{
    HudPopupMessage popup;
    popup.kind = kind;
    popup.text = std::string(message);
    pendingPopupMessages_.push_back(std::move(popup));
}

void Game::clearGameplayInputForChat()
{
    systems::pendingGrenadeThrow = false;
    systems::pendingGrenadeCycleNext = false;
    systems::pendingGrenadeCyclePrev = false;
    systems::prevGrenadeCycleKey = false;
    systems::prevGrenadeThrowKey = false;
    systems::prevGamepadGrenadeCycleKey = false;
    systems::prevGamepadGrenadeThrowKey = false;
    systems::prevGamepadPickupKey = false;
    systems::gamepadPickupHoldFired = false;
    systems::pendingGamepadWeaponSwap = false;
    systems::gamepadLookAccel = 0.0f;
    systems::prevKillSelfKey = false;
    systems::prevAbilitySelectLeft = false;
    systems::prevAbilitySelectRight = false;
    systems::prevGamepadAbilitySelectLeft = false;
    systems::prevGamepadAbilitySelectRight = false;
    pendingScrollSwitch_ = 0;

    registry.view<InputSnapshot, LocalPlayer>().each([](InputSnapshot& snap) {
        snap.forward = false;
        snap.back = false;
        snap.left = false;
        snap.right = false;
        snap.jump = false;
        snap.crouch = false;
        snap.sprint = false;
        snap.grapple = false;
        snap.shooting = false;
        snap.scoped = false;
        snap.reload = false;
        snap.pickup = false;
        snap.switchToPrimary = false;
        snap.switchToSecondary = false;
        snap.killSelf = false;
        snap.skipRespawn = false;
        snap.throwGrenade = false;
        snap.grenadeCycleNext = false;
        snap.grenadeCyclePrev = false;
        snap.ability1 = false;
        snap.ability2 = false;
        snap.abilitySelectHeld = false;
        snap.abilitySelectLeft = false;
        snap.abilitySelectRight = false;
        snap.debugGrantAbilityLevel = false;
    });
}

SDL_AppResult Game::event(SDL_Event* event)
{
    // Forward every event to ImGui first so it can capture keyboard/mouse
    // when the cursor is hovering over a window.
    debugUI.processEvent(event);
    hud_.processEvent(event, userSettings ? &userSettings->inputBindings : nullptr);

    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    // Track the last-used input device so HUD prompts show the matching glyphs.
    // Keyboard/mouse activity flips back to KBM; gamepad buttons or stick/trigger
    // motion past a deadzone flip to Controller (a raw deadzone keeps stick drift
    // from stealing the glyphs from a mouse player who happens to have a pad
    // plugged in).
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
        lastInputDevice_ = BindingDevice::KeyboardMouse;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        lastInputDevice_ = BindingDevice::Controller;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // SDL axes are int16; ~40% deflection clears stick/trigger rest noise.
        if (std::abs(event->gaxis.value) > 13000)
            lastInputDevice_ = BindingDevice::Controller;
        break;
    default:
        break;
    }

    // Resize HUD offscreen target when the window pixel size changes.
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        const auto newW = static_cast<uint32_t>(event->window.data1);
        const auto newH = static_cast<uint32_t>(event->window.data2);
        hud_.resize(newW, newH);
        renderer->setHudTexture(hud_.getOutputTexture());
    }

    if (chatOpen_) {
        if (event->type == SDL_EVENT_TEXT_INPUT) {
            appendBoundedUtf8(chatDraft_, event->text.text);
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_KEY_DOWN) {
            switch (event->key.key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                submitChat();
                return SDL_APP_CONTINUE;
            case SDLK_ESCAPE:
                closeChat();
                return SDL_APP_CONTINUE;
            case SDLK_BACKSPACE:
                popUtf8Codepoint(chatDraft_);
                return SDL_APP_CONTINUE;
            default:
                return SDL_APP_CONTINUE;
            }
        }
        if (event->type == SDL_EVENT_MOUSE_WHEEL || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            return SDL_APP_CONTINUE;
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE && !event->key.repeat) {
            if (pauseMenu.isOpen()) {
                if (userSettings == nullptr || pauseMenu.handleEscape(*userSettings)) {
                    pauseMenu.close();
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(window, true);
                    float dx = 0.0f;
                    float dy = 0.0f;
                    SDL_GetRelativeMouseState(&dx, &dy);
                    clearGameplayInputForChat();
                }
            } else {
                pauseMenu.open();
                mouseCaptured = false;
                SDL_SetWindowRelativeMouseMode(window, false);
                centerMouseInWindow(window);
                clearGameplayInputForChat();
            }
            return SDL_APP_CONTINUE;
        }

        if (pauseMenu.consumeEvent(*event))
            return SDL_APP_CONTINUE;

        switch (event->key.key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!event->key.repeat)
                openChat();
            break;

        case SDLK_MINUS:
            return SDL_APP_SUCCESS;

            // F1 — send a test hello packet to the server.
            // case SDLK_F1: {
            //     static constexpr char k_helloMsg[] = "Hello from client!";
            //     client->send(k_helloMsg, static_cast<int>(sizeof(k_helloMsg) - 1));
            //     SDL_Log("Sent test packet to server");
            //     break;
            // }

        // F2 — toggle the unified debug menu. Individual panels are toggled
        // from checkboxes inside the menu, not all-at-once.
        case SDLK_F2:
            debugUI.toggleDebugMenu();
            // Auto-release cursor when opening the debug menu so the user can
            // interact with ImGui widgets immediately.
            if (debugUI.showDebugMenu && mouseCaptured) {
                mouseCaptured = false;
                SDL_SetWindowRelativeMouseMode(window, false);
            }
            break;

        // F3 — toggle mouse capture on/off independently.
        case SDLK_F3:
            mouseCaptured = !mouseCaptured;
            SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
            break;

        // Particle system test keys
        case SDLK_T: {
            // Energy beam — hits floor or max range
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const float ts = cachedGravFlipped_ ? -1.0f : 1.0f;
            glm::vec3 hip = cachedEye_ + right * (ts * 15.f) - glm::vec3{0, 1, 0} * (ts * 8.f) + cachedCamFwd_ * 5.f;
            if (cachedMuzzleValid_) {
                hip = cachedMuzzleWorld_;
            }
            float dist = 500.f;
            glm::vec3 hitN = -cachedCamFwd_;
            if (cachedCamFwd_.y < -0.001f) {
                const float t = -cachedEye_.y / cachedCamFwd_.y;
                if (t > 0.f && t < dist) {
                    dist = t;
                    hitN = {0, 1, 0};
                }
            }
            const glm::vec3 hitP = cachedEye_ + cachedCamFwd_ * dist;
            particleSystem.spawnHitscanBeam(hip, hitP, WeaponType::EnergyGun);
            particleSystem.spawnImpactEffect(hitP, hitN, SurfaceType::Energy, WeaponType::EnergyGun);
            break;
        }
        case SDLK_Y: {
            // Bullet tracer — hits floor or max range
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const float ds = cachedGravFlipped_ ? -1.0f : 1.0f;
            glm::vec3 hip = cachedEye_ + right * (ds * 15.f) - glm::vec3{0, 1, 0} * (ds * 8.f) + cachedCamFwd_ * 5.f;
            if (cachedMuzzleValid_) {
                hip = cachedMuzzleWorld_;
            }
            float dist = 500.f;
            glm::vec3 hitN = -cachedCamFwd_;
            if (cachedCamFwd_.y < -0.001f) {
                const float t = -cachedEye_.y / cachedCamFwd_.y;
                if (t > 0.f && t < dist) {
                    dist = t;
                    hitN = {0, 1, 0};
                }
            }
            const glm::vec3 hitP = cachedEye_ + cachedCamFwd_ * dist;
            particleSystem.spawnBulletTracer(hip, cachedCamFwd_, dist);
            particleSystem.spawnImpactEffect(hitP, hitN, SurfaceType::Metal, WeaponType::Rifle);
            break;
        }
        case SDLK_U: {
            particleSystem.spawnSmoke(cachedEye_ + cachedCamFwd_ * 200.f, 40.f);
            break;
        }
        case SDLK_I: {
            particleSystem.spawnExplosion(cachedEye_ + cachedCamFwd_ * 300.f, 100.f);
            break;
        }
        case SDLK_O: {
            particleSystem.drawScreenText({10.f, 40.f}, "HP 100  AMMO 30", {1.f, 1.f, 1.f, 1.f}, 24.f);
            break;
        }

        default:
            break;
        }
    }

    // NOTE: Local weapon VFX (tracers, impact, recoil) are handled continuously
    // in iterate() so held fire (auto weapons) spawns effects every cooldown tick.

    // Forward audio-device hot-swap events to the SFX system so it can
    // gracefully reopen when headphones are plugged / unplugged.
    if (event->type == SDL_EVENT_AUDIO_DEVICE_ADDED || event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED ||
        event->type == SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED)
    {
        sfxSystem.handleEvent(*event);
    }

    // ── Gamepad hot-plug ──────────────────────────────────────────────────
    // Runtime connect/disconnect while in-game. SDL also fires _ADDED for pads
    // already connected at SDL_Init time, but those arrive before this Game
    // screen exists and are delivered to the lobby instead — the pre-connected
    // case is handled by scanForConnectedGamepads() in Game::init(). We accept
    // the first pad and ignore additional ones; multi-controller support
    // (split-screen / co-op) would need an entity-per-controller scheme in the
    // ECS, out of scope for this change.
    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        adoptGamepad(event->gdevice.which);
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        // Only tear down if the disconnected device is the one we're using —
        // otherwise an unrelated unplug (e.g. a second pad we never opened)
        // would leave us with a dangling-but-non-null handle.
        if (activeGamepad_ && event->gdevice.which == activeGamepadId_) {
            SDL_Log("[input] gamepad disconnected (id=%u)", activeGamepadId_);
            SDL_Gamepad* disconnected = activeGamepad_;
            activeGamepad_ = nullptr;
            activeGamepadId_ = 0;
            systems::prevGamepadAbilitySelectLeft = false;
            systems::prevGamepadAbilitySelectRight = false;
            SDL_CloseGamepad(disconnected);
        }
    }

    // Gamepad Start (right menu button) toggles the pause menu, mirroring ESC.
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
        if (pauseMenu.isOpen()) {
            if (userSettings == nullptr || pauseMenu.handleEscape(*userSettings)) {
                pauseMenu.close();
                mouseCaptured = true;
                SDL_SetWindowRelativeMouseMode(window, true);
                clearGameplayInputForChat();
            }
        } else {
            pauseMenu.open();
            mouseCaptured = false;
            SDL_SetWindowRelativeMouseMode(window, false);
            centerMouseInWindow(window);
            clearGameplayInputForChat();
        }
        return SDL_APP_CONTINUE;
    }

    if (pauseMenu.consumeEvent(*event))
        return SDL_APP_CONTINUE;

    // Configurable wheel/button events can toggle between primary and secondary
    // weapon slots. Mouse wheel (KBM) and gamepad buttons (e.g. Y/North) both
    // route through eventMatches; NextWeapon flips to the other slot, acting as
    // a single "toggle gun" press on the controller.
    if ((event->type == SDL_EVENT_MOUSE_WHEEL || event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) && mouseCaptured &&
        userSettings)
    {
        bool down = false;
        if (userSettings->inputBindings.eventMatches(Action::PreviousWeapon, *event, down) && down)
            pendingScrollSwitch_ = -1;
        else if (userSettings->inputBindings.eventMatches(Action::NextWeapon, *event, down) && down)
            pendingScrollSwitch_ = 1;
    }

    // Re-capture mouse on window click while uncaptured (standard FPS behaviour).
    // Suppress recapture when ImGui wants the mouse (hovering over a debug
    // window) or when the debug menu is open — let the user interact freely.
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse && !debugUI.showDebugMenu) {
            mouseCaptured = true;
            SDL_SetWindowRelativeMouseMode(window, true);
        }
    }

    return SDL_APP_CONTINUE;
}

/// @brief Advance one frame: decoupled physics / render loop.
///
/// Physics ALWAYS runs at exactly 128 Hz (k_physicsHz) using an accumulator
/// with a multi-tick catch-up loop (up to k_maxTicksPerFrame per call).
/// This is non-negotiable: it must match the server tick rate.
///
/// Input is split into two independent streams:
///
///   Mouse look (yaw / pitch) -- sampled EVERY iterate() call so camera
///       rotation is perfectly smooth at whatever frame rate the renderer
///       produces.  The camera always uses the latest yaw directly (never
///       interpolated).  Interpolating yaw with the physics alpha creates a
///       timebase mismatch on multi-tick or zero-tick frames, producing
///       visible jitter.
///
///   Movement keys (WASD / jump / crouch) -- sampled once per physics tick
///       group when inputSyncedWithPhysics is true (the default) so
///       movement calculations match the server.  When the toggle is off,
///       keys are also sampled every iterate() call.
///
/// Position interpolation uses alpha = accumulator / k_physicsDt across the
/// LAST physics tick (PreviousPosition is saved inside the while loop before
/// each tick).
///
/// Three ImGui-tunable flags:
///
///   renderSeparateFromPhysics -- render every iterate() call with position
///       interpolated between the last two physics ticks (true, default) vs.
///       render only after a physics tick (false, caps render fps at 128 Hz).
///
///   inputSyncedWithPhysics -- sample movement keys once per tick group
///       (true, default, server-consistent) vs. every iterate() call (false).
///       Mouse look is always per-frame regardless of this toggle.
///
///   limitFPSToMonitor -- when ON and monitor >= physicsHz, uses VSync.
///       When monitor < physicsHz (regardless of this toggle), a software
///       frame limiter at physicsHz is always active to ensure rock-steady
///       frame pacing — the monitor can't display above its refresh rate
///       anyway, and uncapped rendering creates beat-frequency jitter.

void Game::applyFrameRateLimit()
{
    // Query the monitor's native refresh rate.
    int monitorHz = 60; // safe fallback
    const SDL_DisplayID displayID = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayID);
    if (mode && mode->refresh_rate > 0.0f)
        monitorHz = static_cast<int>(std::ceil(mode->refresh_rate));

    if (limitFPSToMonitor && monitorHz >= k_physicsHz) {
        // Monitor is fast enough; rely on the renderer's default present mode.
        softLimitPeriod = 0;
        if (renderer)
            renderer->setVSync(true);
    } else if (monitorHz < k_physicsHz) {
        softLimitPeriod = SDL_GetPerformanceFrequency() / static_cast<Uint64>(k_physicsHz);
        softLimitNextFrame = SDL_GetPerformanceCounter() + softLimitPeriod;
        if (renderer)
            renderer->setVSync(false);
        SDL_Log(
            "[client] monitor %d Hz < physics %d Hz — software limiter at %d fps", monitorHz, k_physicsHz, k_physicsHz);
    } else {
        softLimitPeriod = 0;
        if (renderer)
            renderer->setVSync(false);
    }
}

void Game::adoptGamepad(SDL_JoystickID id)
{
    if (activeGamepad_)
        return; // First-device-wins: a controller is already bound.
    activeGamepad_ = SDL_OpenGamepad(id);
    if (activeGamepad_) {
        activeGamepadId_ = id;
        const char* name = SDL_GetGamepadName(activeGamepad_);
        SDL_Log("[input] gamepad connected: %s (id=%u)", name ? name : "unknown", id);
    } else {
        SDL_Log("[input] SDL_OpenGamepad failed for id=%u: %s", id, SDL_GetError());
    }
}

void Game::spawnMuzzleFlashLight(const glm::vec3& pos)
{
    // Respect the "Muzzle Flash" setting — skip spawning when disabled. Lights
    // already in flight keep fading out naturally.
    if (userSettings != nullptr && !userSettings->muzzleFlashEnabled)
        return;

    TransientVfxLight flash;
    flash.position = pos;
    // Warm orange-white muzzle flash. Intensity is in the same scale as the
    // scene's static point lights (shader attenuation is pure 1/r²), so it
    // needs to be large to noticeably light nearby surfaces.
    flash.color = glm::vec3(1.0f, 0.65f, 0.30f);
    flash.intensity = 4500.0f;
    flash.age = 0.0f;
    // ~200 ms total: a ~20 ms ease-in to full brightness then a gradual ~180 ms
    // exponential die-out (see the attack-decay envelope in iterate()). Long
    // enough that consecutive shots' flashes overlap into a continuous glow
    // during rapid fire, while a single shot still reads as a quick flash.
    flash.lifetime = 0.20f;
    flash.range = 500.0f;
    transientVfxLights_.push_back(flash);
}

void Game::spawnExplosionFlashLight(const glm::vec3& pos, WeaponType weaponType, float radius)
{
    TransientVfxLight flash;
    flash.position = pos;
    flash.age = 0.0f;
    flash.range = std::clamp(radius * 3.0f, 300.0f, 900.0f);

    switch (weaponType) {
    case WeaponType::Sticky:
        flash.color = glm::vec3{0.18f, 0.70f, 1.0f};
        flash.intensity = 9800.0f;
        flash.lifetime = 0.14f;
        break;
    case WeaponType::HEGrenade:
        flash.color = glm::vec3{1.0f, 0.72f, 0.34f};
        flash.intensity = 7600.0f;
        flash.lifetime = 0.12f;
        break;
    case WeaponType::Molotov:
        flash.color = glm::vec3{1.0f, 0.38f, 0.08f};
        flash.intensity = 6200.0f;
        flash.lifetime = 0.24f;
        break;
    case WeaponType::Rocket:
    default:
        flash.color = glm::vec3{1.0f, 0.56f, 0.18f};
        flash.intensity = 11000.0f;
        flash.lifetime = 0.18f;
        break;
    }
    transientVfxLights_.push_back(flash);
}

void Game::scanForConnectedGamepads()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) {
        // Non-fatal: no gamepad subsystem / enumeration failure. Hot-plug still
        // works via SDL_EVENT_GAMEPAD_ADDED.
        SDL_Log("[input] SDL_GetGamepads failed: %s", SDL_GetError());
        return;
    }
    for (int i = 0; i < count && !activeGamepad_; ++i)
        adoptGamepad(ids[i]);
    SDL_free(ids);
}

SDL_AppResult Game::iterate()
{
    if (activeGamepad_ && !systems::gamepadConnected(activeGamepad_)) {
        SDL_Log("[input] gamepad disconnected without removal event (id=%u)", activeGamepadId_);
        SDL_Gamepad* disconnected = activeGamepad_;
        activeGamepad_ = nullptr;
        activeGamepadId_ = 0;
        systems::prevGamepadAbilitySelectLeft = false;
        systems::prevGamepadAbilitySelectRight = false;
        SDL_CloseGamepad(disconnected);
    }

    // Phase-1 debug viz: drain the previous frame's contact accumulator and
    // start a new bucket.  All `physics::debug::pushContact(...)` calls during
    // the upcoming sim ticks accumulate here; the foreground overlay reads
    // them later in the render block.  No-op when capture is disabled.
    physics::debug::beginFrame();

    // 1. Accumulate real elapsed time
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_now = SDL_GetPerformanceCounter();

    // Per-frame phase timer state.  Completely dormant unless bench mode or
    // GROUP2_CLIENT_PERF=1 is active; disabled builds pay one branch per marker.
    ClientPerfFrame phaseStats{};
    const bool collectPerf = benchActive_ || perfRecorder_.isRecording();
    physics::perf::setEnabled(collectPerf);
    if (collectPerf) {
        physics::perf::resetFrame();
        perfSnapshotApplyMs_ = 0.0f;
        perfSnapshotApplyCount_ = 0;
    }
    Uint64 phaseLastTick = k_now;
    const auto phaseSnap = [&](float& outMs) {
        if (!collectPerf)
            return;
        const Uint64 nowTick = SDL_GetPerformanceCounter();
        outMs = static_cast<float>(nowTick - phaseLastTick) * 1000.0f / static_cast<float>(k_perfFreq);
        phaseLastTick = nowTick;
    };

    float frameTime = static_cast<float>(k_now - prevTime) / static_cast<float>(k_perfFreq);

    // If the game was suspended (backgrounded / minimized), the raw delta can
    // be enormous.  Rather than trying to catch up through potentially seconds
    // of physics ticks (and replaying every buffered TCP message visually), we
    // detect the gap and skip straight to the present.  The next client->poll()
    // will still drain the TCP buffer so entity state snaps to current.
    static constexpr float k_suspendThreshold = 0.5f; // half-second gap = clearly suspended
    if (frameTime > k_suspendThreshold) {
        SDL_Log("[client] detected suspend (gap %.2fs) — resetting accumulator", static_cast<double>(frameTime));
        prevTime = k_now;
        accumulator = k_physicsDt; // run exactly one tick to apply latest state
        // Drain the network now so we snap to the current server state
        // without fast-forwarding through every intermediate update.
        if (!client->poll()) {
            SDL_Log("Game: lost connection to server; returning to main menu");
            returnToMainMenuRequested_ = true;
            serverShutdownNoticeRequested_ = true;
            return SDL_APP_CONTINUE;
        }
        refreshRemotePlayerRenderables();
        refreshRemoteProjectileRenderables();
        refreshRemoteRespawnRenderables();
        refreshDroppedWeaponRenderables();
        refreshRemoteHealthPackRenderables();
        // Fall through to render the current frame normally.
    } else {
        frameTime = std::min(frameTime, 0.25f); // cap to avoid spiral-of-death
        prevTime = k_now;
        accumulator += frameTime;
    }
    if (collectPerf) {
        phaseStats.frameNumber = frameCount;
        phaseStats.timestampMs = static_cast<double>(SDL_GetTicksNS()) / 1000000.0;
        phaseStats.wallFrameMs = frameTime * 1000.0f;
    }

    // Hot-reload: poll weapon hold-pose TOMLs at ~4 Hz. If the mtime moved,
    // reload the pose in place. Filesystem stats are cheap, but doing them every
    // frame is wasteful — the throttle keeps the editing loop responsive while
    // staying well under the cost of one rig joint update.
    holdPoseReloadAccumulator_ += frameTime;
    if (holdPoseReloadAccumulator_ >= 0.25f) {
        holdPoseReloadAccumulator_ = 0.0f;
        for (std::size_t i = 0; i < weaponHoldPoses_.size(); ++i) {
            if (weaponHoldPosePaths_[i].empty())
                continue;
            std::error_code ec;
            const auto mtime = std::filesystem::last_write_time(weaponHoldPosePaths_[i], ec);
            if (ec)
                continue;
            if (mtime != weaponHoldPoseMTimes_[i]) {
                weaponHoldPoseMTimes_[i] = mtime;
                // Reload into a scratch struct so a partial/parse-failed load
                // doesn't blank out the working data already in weaponHoldPoses_.
                WeaponHoldPose scratch = getWeaponHoldPose(static_cast<WeaponType>(i));
                if (loadWeaponHoldPose(weaponHoldPosePaths_[i], scratch))
                    weaponHoldPoses_[i] = scratch;
            }
        }
    }

    static int iterCount = 0;
    if (false && ++iterCount <= 3)
        SDL_Log("[ITERATE] call=%d frameTime=%.4f acc=%.4f renderSep=%d",
                iterCount,
                static_cast<double>(frameTime),
                static_cast<double>(accumulator),
                renderSeparateFromPhysics);

    // 2. Refresh performance stats every 0.5 s
    static constexpr float k_statsPeriod = 0.5f;
    const float statsDt = static_cast<float>(k_now - statsPrevTime) / static_cast<float>(k_perfFreq);
    if (statsDt >= k_statsPeriod && fpsHistoryCount > 0) {
        // Physics rate: tick count / elapsed.
        measuredPhysicsHz = static_cast<float>(statsPhysTicks) / statsDt;
        statsPhysTicks = 0;

        // Current FPS: actual rendered frames / elapsed time over the window —
        // a true average, not a single noisy 1/dt snapshot of the last frame.
        statsFPSCurrent = static_cast<float>(statsRenderFrames) / statsDt;
        statsRenderFrames = 0;

        statsPrevTime = k_now;

        // FPS percentile stats from the ring buffer.
        const int count = fpsHistoryCount; // may be < k_fpsHistorySize
        float sorted[k_fpsHistorySize];
        if (count < k_fpsHistorySize) {
            for (int i = 0; i < count; ++i)
                sorted[i] = fpsHistory[i];
        } else {
            // Full ring: oldest sample is at fpsHistoryHead.
            for (int i = 0; i < k_fpsHistorySize; ++i)
                sorted[i] = fpsHistory[(fpsHistoryHead + i) % k_fpsHistorySize];
        }
        std::sort(sorted, sorted + count); // ascending: worst fps first

        statsFPSMin = sorted[0];
        statsFPSMax = sorted[count - 1];
        statsFPS1pLow = sorted[static_cast<int>(static_cast<float>(count) * 0.01f)]; // 1st percentile
        statsFPS5pLow = sorted[static_cast<int>(static_cast<float>(count) * 0.05f)]; // 5th percentile
    }

    // Bench mode: collect per-frame timings (after warmup) then aggregate +
    // print + quit when the duration is up.
    if (benchActive_) {
        const float benchElapsed = static_cast<float>(k_now - benchStartTime_) / static_cast<float>(k_perfFreq);
        // Record this frame's wall-clock time once we're past the warmup window.
        if (benchElapsed >= k_benchWarmupSeconds && frameTime > 0.0f && frameTime < 0.25f)
            benchFrameTimesMs_.push_back(frameTime * 1000.0f);

        if (benchElapsed >= benchSeconds_) {
            const auto countSamples = benchFrameTimesMs_.size();
            if (countSamples == 0) {
                std::fprintf(stderr, "[bench] no samples collected (warmup window > duration?)\n");
                return SDL_APP_SUCCESS;
            }

            // Sort ascending: low ms = fast frames, high ms = slow frames.
            // We'll convert to FPS (=1000/ms) for the summary, so worst-frame
            // FPS = 1000 / max-ms = the LOW percentiles.
            auto frames = benchFrameTimesMs_;
            std::sort(frames.begin(), frames.end());
            auto pct = [&](float p) {
                const auto idx = static_cast<size_t>(static_cast<float>(countSamples - 1) * p);
                return frames[idx];
            };
            const float msMedian = pct(0.50f);
            const float msAvg = std::accumulate(frames.begin(), frames.end(), 0.0f) / static_cast<float>(countSamples);
            const float msP95 = pct(0.95f); // 95th-percentile slowest = 5% lows
            const float msP99 = pct(0.99f); // 99th-percentile slowest = 1% lows
            const float msFastest = frames.front();
            const float msSlowest = frames.back();

            auto fps = [](float ms) { return ms > 0.0f ? 1000.0f / ms : 0.0f; };

            std::fprintf(stderr,
                         "[bench] elapsed=%.1fs samples=%zu "
                         "avg=%.1f median=%.1f p5=%.1f p1=%.1f min=%.1f max=%.1f\n",
                         static_cast<double>(benchElapsed),
                         countSamples,
                         static_cast<double>(fps(msAvg)),
                         static_cast<double>(fps(msMedian)),
                         static_cast<double>(fps(msP95)),
                         static_cast<double>(fps(msP99)),
                         static_cast<double>(fps(msSlowest)),
                         static_cast<double>(fps(msFastest)));

            // Phase breakdown: split frames into "fast median band" (frames
            // near the median) vs. "slowest 1%" (the p1 tail).  Average each
            // phase across each band, plus dump a few representative slow
            // frames so we can see the actual stalls.
            if (!benchFrameStats_.empty()) {
                auto sortedStats = benchFrameStats_;
                std::sort(
                    sortedStats.begin(), sortedStats.end(), [](const ClientPerfFrame& a, const ClientPerfFrame& b) {
                        return a.cpuFrameMs < b.cpuFrameMs;
                    });

                const size_t n = sortedStats.size();
                const size_t medLo = static_cast<size_t>(static_cast<float>(n - 1) * 0.45f);
                const size_t medHi = static_cast<size_t>(static_cast<float>(n - 1) * 0.55f);
                const size_t slowLo = static_cast<size_t>(static_cast<float>(n - 1) * 0.99f);

                ClientPerfFrame medAvg{}, slowAvg{};
                size_t medCount = 0, slowCount = 0;
                for (size_t i = medLo; i <= medHi; ++i) {
                    medAvg.cpuFrameMs += sortedStats[i].cpuFrameMs;
                    medAvg.inputMs += sortedStats[i].inputMs;
                    medAvg.physicsMs += sortedStats[i].physicsMs;
                    medAvg.networkPollMs += sortedStats[i].networkPollMs;
                    medAvg.particlesMs += sortedStats[i].particlesMs;
                    medAvg.animationMs += sortedStats[i].animationMs;
                    medAvg.entityCmdsMs += sortedStats[i].entityCmdsMs;
                    medAvg.imguiMs += sortedStats[i].imguiMs;
                    medAvg.drawFrameMs += sortedStats[i].drawFrameMs;
                    ++medCount;
                }
                for (size_t i = slowLo; i < n; ++i) {
                    slowAvg.cpuFrameMs += sortedStats[i].cpuFrameMs;
                    slowAvg.inputMs += sortedStats[i].inputMs;
                    slowAvg.physicsMs += sortedStats[i].physicsMs;
                    slowAvg.networkPollMs += sortedStats[i].networkPollMs;
                    slowAvg.particlesMs += sortedStats[i].particlesMs;
                    slowAvg.animationMs += sortedStats[i].animationMs;
                    slowAvg.entityCmdsMs += sortedStats[i].entityCmdsMs;
                    slowAvg.imguiMs += sortedStats[i].imguiMs;
                    slowAvg.drawFrameMs += sortedStats[i].drawFrameMs;
                    ++slowCount;
                }
                auto avgRow = [&](ClientPerfFrame& s, size_t c) {
                    if (c == 0)
                        return;
                    s.cpuFrameMs /= static_cast<float>(c);
                    s.inputMs /= static_cast<float>(c);
                    s.physicsMs /= static_cast<float>(c);
                    s.networkPollMs /= static_cast<float>(c);
                    s.particlesMs /= static_cast<float>(c);
                    s.animationMs /= static_cast<float>(c);
                    s.entityCmdsMs /= static_cast<float>(c);
                    s.imguiMs /= static_cast<float>(c);
                    s.drawFrameMs /= static_cast<float>(c);
                };
                avgRow(medAvg, medCount);
                avgRow(slowAvg, slowCount);

                std::fprintf(stderr,
                             "[bench] median-band  total=%5.2fms phys=%4.2f net=%4.2f anim=%4.2f "
                             "part=%4.2f ent=%4.2f ui=%4.2f draw=%5.2f\n",
                             static_cast<double>(medAvg.cpuFrameMs),
                             static_cast<double>(medAvg.physicsMs),
                             static_cast<double>(medAvg.networkPollMs),
                             static_cast<double>(medAvg.animationMs),
                             static_cast<double>(medAvg.particlesMs),
                             static_cast<double>(medAvg.entityCmdsMs),
                             static_cast<double>(medAvg.imguiMs),
                             static_cast<double>(medAvg.drawFrameMs));
                std::fprintf(stderr,
                             "[bench] slowest-1%%  total=%5.2fms phys=%4.2f net=%4.2f anim=%4.2f "
                             "part=%4.2f ent=%4.2f ui=%4.2f draw=%5.2f\n",
                             static_cast<double>(slowAvg.cpuFrameMs),
                             static_cast<double>(slowAvg.physicsMs),
                             static_cast<double>(slowAvg.networkPollMs),
                             static_cast<double>(slowAvg.animationMs),
                             static_cast<double>(slowAvg.particlesMs),
                             static_cast<double>(slowAvg.entityCmdsMs),
                             static_cast<double>(slowAvg.imguiMs),
                             static_cast<double>(slowAvg.drawFrameMs));

                // Top 5 slowest frames, full breakdown — to spot one-off
                // stalls (e.g. a single drawFrame=120ms in an otherwise
                // tight set of slow frames means a true GPU stall, not a
                // CPU section regressing).
                std::fprintf(stderr, "[bench] top-5 slowest individual frames:\n");
                for (size_t i = (n >= 5 ? n - 5 : 0); i < n; ++i) {
                    const auto& s = sortedStats[i];
                    std::fprintf(stderr,
                                 "[bench]   total=%5.2fms phys=%4.2f net=%4.2f anim=%4.2f part=%4.2f ent=%4.2f "
                                 "ui=%4.2f draw=%5.2f (acq=%4.2f rec=%4.2f sub=%4.2f)\n",
                                 static_cast<double>(s.cpuFrameMs),
                                 static_cast<double>(s.physicsMs),
                                 static_cast<double>(s.networkPollMs),
                                 static_cast<double>(s.animationMs),
                                 static_cast<double>(s.particlesMs),
                                 static_cast<double>(s.entityCmdsMs),
                                 static_cast<double>(s.imguiMs),
                                 static_cast<double>(s.drawFrameMs),
                                 static_cast<double>(s.drawAcquireMs),
                                 static_cast<double>(s.drawRecordMs),
                                 static_cast<double>(s.drawSubmitMs));
                }
            }

            std::fflush(stderr);
            return SDL_APP_SUCCESS;
        }
    }

    // Mark the start of the input phase.  Anything before this (suspend
    // handling, stats ring update, bench summary check) is the preamble.
    if (collectPerf) {
        const Uint64 preambleEnd = SDL_GetPerformanceCounter();
        phaseStats.preambleMs =
            static_cast<float>(preambleEnd - phaseLastTick) * 1000.0f / static_cast<float>(k_perfFreq);
        phaseLastTick = preambleEnd;
    }

    // 3. Input
    //
    // Mouse look runs EVERY iterate() call — this keeps camera rotation
    // perfectly smooth at whatever frame rate the renderer is producing.
    // SDL_GetRelativeMouseState returns accumulated delta since last call,
    // so total rotation is identical regardless of call frequency.
    //
    // Movement keys run once per physics tick group (when inputSyncedWithPhysics
    // is true) so WASD movement calculations match the server.  When the
    // sync toggle is off, movement keys also run every frame.
    // Dead input runs regardless of mouse capture — allows skip-respawn.
    // Chat owns the keyboard while open, so gameplay inputs are cleared
    // instead of sampled.
    const bool gamePaused = pauseMenu.isOpen();
    // Look-only phases (warmup/countdown) allow camera rotation but still
    // suppress weapon/movement input. FINISHED and LOBBY suppress everything.
    const bool lookInputAllowed = currentMatchPhase == MatchPhase::WARMUP ||
                                  currentMatchPhase == MatchPhase::COUNTDOWN ||
                                  currentMatchPhase == MatchPhase::IN_PROGRESS;
    const bool gameplayInputAllowed = currentMatchPhase == MatchPhase::IN_PROGRESS;

    if (gamePaused || !gameplayInputAllowed) {
        clearGameplayInputForChat();
    } else if (chatOpen_)
        clearGameplayInputForChat();
    else {
        systems::runDeadInput(registry, userSettings->inputBindings);
        systems::runGamepadDeadInput(registry, activeGamepad_, userSettings->inputBindings);
    }

    // Query local player's gravity flip state — used for mouse/stick
    // inversion AND for swapping A-D / left-stick left-right.
    bool localGravFlipped = false;
    registry.view<PlayerVisState, LocalPlayer>().each(
        [&](const PlayerVisState& vis) { localGravFlipped = vis.gravityFlipped; });

    // On respawn, snap the local view to the spawn point's authored facing so
    // the player doesn't spawn looking into a wall. Fires once on the dead→alive
    // edge; runMouseLook is incremental (yaw -= mdx*sens) so this frame's mouse
    // delta still composes on top and the player immediately keeps control.
    registry.view<PlayerVisState, LocalPlayer, InputSnapshot>().each(
        [&](const PlayerVisState& vis, InputSnapshot& snap) {
            if (localWasDead_ && !vis.isDead) {
                snap.yaw = vis.spawnViewYaw;
                snap.pitch = 0.0f;
            }
            if (vis.isDead)
                localEmote_ = -1; // No emoting while dead.
            localWasDead_ = vis.isDead;
        });

    if (mouseCaptured && !chatOpen_ && !gamePaused && lookInputAllowed) {

        // Emote wheel must run before the look samplers so it can divert pointer
        // motion into sector selection while open (and suppress camera turn).
        if (gameplayInputAllowed) {
            systems::runEmoteWheelKey(userSettings->inputBindings);
            systems::runGamepadEmoteWheel(activeGamepad_, userSettings->inputBindings, userSettings->gamepadSwapSticks);
        }

        systems::runMouseLook(registry, mouseSensitivity, localGravFlipped);
        if (gameplayInputAllowed) {
            if (!inputSyncedWithPhysics)
                systems::runMovementKeys(registry, userSettings->inputBindings, localGravFlipped);
            systems::runWeaponKeys(registry, userSettings->inputBindings);
        }

        // Gamepad samplers run AFTER kbm so they OR into the same flags —
        // a player can use kbm and pad simultaneously without either source
        // stomping the other.  Look additively composes (mouse delta + stick
        // delta) so the camera tracks whichever input is moving.  No-ops
        // cheaply when activeGamepad_ is nullptr.
        systems::runGamepadLook(registry,
                                activeGamepad_,
                                userSettings->gamepadPitchSensitivity,
                                userSettings->gamepadYawSensitivity,
                                userSettings->gamepadLookDeadzone,
                                frameTime,
                                localGravFlipped,
                                userSettings->gamepadSwapSticks);
        // AAA-style aim assist runs IMMEDIATELY after the look sampler so it
        // can refund part of the just-applied look delta (slowdown) and add
        // a rotational pull derived from the *change* in the target's
        // angular position frame-to-frame.  Mouse-only players see no
        // effect — `activeGamepad_` is nullptr and the function early-outs.
        // Both effects are gated on stick actuation ≥ 5 % so a player
        // holding still keeps full manual control.
        if (userSettings->aimAssistEnabled) {
            systems::GamepadAimAssistConfig effectiveAimAssistCfg = aimAssistCfg_;
            const float aimAssistStrength = std::clamp(userSettings->aimAssistStrength, 0.0f, 1.0f);
            effectiveAimAssistCfg.enabled = userSettings->aimAssistEnabled;
            effectiveAimAssistCfg.rotationalCompensation *= aimAssistStrength;
            effectiveAimAssistCfg.slowdownStrength =
                std::clamp(1.0f - (1.0f - aimAssistCfg_.slowdownStrength) * aimAssistStrength, 0.0f, 1.0f);
            systems::runGamepadAimAssist(registry,
                                         activeGamepad_,
                                         effectiveAimAssistCfg,
                                         aimAssistState_,
                                         userSettings->gamepadPitchSensitivity,
                                         userSettings->gamepadYawSensitivity,
                                         userSettings->gamepadLookDeadzone,
                                         userSettings->gamepadMoveDeadzone,
                                         frameTime,
                                         userSettings->gamepadSwapSticks);
        }
        if (gameplayInputAllowed) {
            if (!inputSyncedWithPhysics)
                systems::runGamepadMovement(registry,
                                            activeGamepad_,
                                            userSettings->inputBindings,
                                            userSettings->gamepadMoveDeadzone,
                                            localGravFlipped,
                                            userSettings->gamepadSwapSticks);
            systems::runGamepadWeapon(registry, activeGamepad_, userSettings->inputBindings);
        }
    } else {
        // Gameplay input is suppressed (paused / chat / menu) — make sure the
        // emote wheel doesn't get stuck open with no way to release it.
        systems::emoteWheelOpen = false;
        systems::emoteWheelSelection = -1;
        systems::prevEmoteKey = false;
        systems::prevGamepadEmoteKey = false;
    }

    // Network stats: send periodic pings and update bandwidth counters
    phaseSnap(phaseStats.inputMs);

    client->updateStats(frameTime);
    pingTimer += frameTime;
    if (pingTimer >= 1.0f) {
        client->sendPing();
        pingTimer = 0.0f;
    }
    phaseSnap(phaseStats.networkStatsMs);

    // 4. Physics -- always 128 Hz, up to k_maxTicksPerFrame catch-up
    bool physicsRan = false;
    int ticksThisFrame = 0;
    bool grantAbilityLevelThisFrame = false;
    bool throwGrenadeThisFrame = false;
    bool grenadeCycleNextThisFrame = false;
    bool grenadeCyclePrevThisFrame = false;
    int emoteRequestThisFrame = -1;

    if (accumulator >= k_physicsDt) {
        grantAbilityLevelThisFrame = debugUI.pendingAbilityLevelGrant_;
        debugUI.pendingAbilityLevelGrant_ = false;
        throwGrenadeThisFrame = systems::consumePendingGrenadeThrow();
        grenadeCycleNextThisFrame = systems::consumePendingGrenadeCycleNext();
        grenadeCyclePrevThisFrame = systems::consumePendingGrenadeCyclePrev();
        emoteRequestThisFrame = systems::consumePendingEmote();
        // Predict the emote locally for instant third-person feedback; the
        // server confirms it for everyone else via PlayerVisState/AnimSnapshot.
        if (emoteRequestThisFrame >= 0)
            localEmote_ = emoteRequestThisFrame;

        // A quick tap of gamepad Y latches a weapon swap (hold-Y is pickup,
        // handled in runGamepadWeapon). Fold it into the same scroll-switch path.
        if (systems::consumePendingGamepadWeaponSwap())
            pendingScrollSwitch_ = 1;

        // Apply scroll-wheel / button weapon switch (mouse wheel, gamepad Y tap),
        // constrained to primary/secondary. Consumed HERE — inside the
        // physics-tick gate — rather than every iterate, so a press is only
        // cleared on a frame that actually stamps and sends input. Consuming it
        // in the per-frame block dropped presses on render frames that ran no
        // physics tick (common above 128 fps), which made the toggle fire only
        // ~128/FPS of the time. pendingScrollSwitch_ now persists across no-tick
        // frames until a tick consumes it.
        if (pendingScrollSwitch_ != 0) {
            registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) {
                int slotIdx = 0;
                registry.view<LocalPlayer, WeaponState>().each(
                    [&](const WeaponState& ws) { slotIdx = static_cast<int>(ws.current); });

                slotIdx = (slotIdx + pendingScrollSwitch_ + 2) % 2;
                snap.switchToPrimary = (slotIdx == 0);
                snap.switchToSecondary = (slotIdx == 1);
            });
            pendingScrollSwitch_ = 0;
        }

        // Movement keys: sample once for this whole group of ticks.
        if (inputSyncedWithPhysics && mouseCaptured && !chatOpen_ && !gamePaused && gameplayInputAllowed) {
            systems::runMovementKeys(registry, userSettings->inputBindings, localGravFlipped);
            // Gamepad movement is sampled on the same cadence and ORs into
            // the same flags so kbm + pad stay coherent under physics-sync.
            systems::runGamepadMovement(registry,
                                        activeGamepad_,
                                        userSettings->inputBindings,
                                        userSettings->gamepadMoveDeadzone,
                                        localGravFlipped,
                                        userSettings->gamepadSwapSticks);
        }

        // PR-24 (off-by-one + capsule staleness fix): the fire detection
        // and capture block used to live HERE, before the physics while
        // loop.  Two regressions:
        //   1. snap.tick was the OLD value (pre-increment), but the
        //      wire-format input (sent in `runInputSend` after the
        //      while loop) carries the NEW value.  Server's debug
        //      capture stamped the new value → tick mismatch → debug
        //      UI showed two ring slots for the same shot.  PR-20.1
        //      had originally fixed this by capturing AFTER the stamp;
        //      PR-20.8's jitter fix moved the stamp into the while
        //      loop but left the capture before it.
        //   2. The captured capsules were from the PREVIOUS frame's
        //      `updateHitboxes` (the current frame's `applyInterpolated
        //      Transforms` + `updateHitboxes` haven't run yet).  At
        //      60 fps that's ~16 ms staler than what the user sees on
        //      screen — the BLUE capsules in the debug overlay didn't
        //      reflect what the player was actually aiming at.
        //
        // Both fixed in PR-24 by moving the entire capture to right
        // after `systems::updateHitboxes` (search for "PR-24 fire
        // detection" further down).  We still need the rising-edge
        // detector to advance `prevShootingForDebug_` here so it stays
        // exactly once per iterate (the post-physics block reads the
        // SAME `snap.shooting` we set in `runWeaponKeys` above).
        physicsRan = true;

        // PR-20.8 (jitter root-cause): tick-stamp + ring-push + send
        // moved INSIDE the per-physics-tick loop.  Each physics tick
        // gets its own `clientPredictTick`, its own InputSnapshot
        // tick stamp, and its own ring entry.  Reconciliation's 1:1
        // replay (one `runMovement` per stored tick) now exactly
        // matches prediction's 1:1 advance (one `runMovement` per
        // physics tick).  Pre-PR-20.8 the increment was OUTSIDE the
        // loop while runPrediction ran N times per iterate at
        // 60 fps × 128 Hz physics, making replay always under-run
        // by `(N-1) × dt × velocity` per stored tick.
        //
        // Phase 5a/b: PreviousPosition is updated by Client::
        // dispatchMessage when a snapshot arrives, not every physics
        // tick.  The renderer interpolates over the snapshot interval
        // via `client->getSnapshotAlpha()` so motion stays smooth at
        // the much-coarser snapshot rate.  Per physics tick we ALSO
        // snapshot the local player's pos→prev BEFORE running
        // prediction so the renderer shows tick-rate-smooth
        // interpolation of the local player.
        while (accumulator >= k_physicsDt && ticksThisFrame < k_maxTicksPerFrame) {
            accumulator -= k_physicsDt;

            // Per-physics-tick: increment + stamp + ring push.
            // All physics ticks in this iterate see the same input
            // sampled at runWeaponKeys/runMovementKeys time above —
            // we just give each tick its own monotonic tick number.
            ++clientPredictTick;
            registry.view<InputSnapshot, LocalPlayer>().each([this, grantAbilityLevelThisFrame](InputSnapshot& snap) {
                snap.tick = clientPredictTick;
                snap.debugGrantAbilityLevel = grantAbilityLevelThisFrame;
                // Grenade throw/cycle are edge events: keep them false during
                // prediction ticks so the local sim never double-applies them.
                // They're stamped onto the sent snapshot once, after the loop.
                snap.throwGrenade = false;
                snap.grenadeCycleNext = false;
                snap.grenadeCyclePrev = false;
            });
            registry.view<LocalPlayer, InputSnapshot>().each(
                [this](const InputSnapshot& snap) { inputRing_.push(clientPredictTick, snap); });

            // Capture local pos→prev for tick-rate interp, then run
            // client-side prediction.  `PlayerSimState` filter on
            // runMovement narrows to just the local player (remotes
            // don't have `PlayerSimState` on the client).
            registry.view<LocalPlayer, Position, PreviousPosition>().each(
                [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });
            if (gameplayInputAllowed) {
                systems::runPrediction(registry, k_physicsDt, physics::activeWorld());
            } else {
                registry.view<LocalPlayer, Velocity>().each([](Velocity& vel) { vel = Velocity{}; });
            }
            storePredictedPlayerState(clientPredictTick);

            ++tickCount;
            ++ticksThisFrame;
            ++statsPhysTicks;
        }

        // Send the redundant input batch ONCE per iterate.  After the
        // physics loop has stamped the LATEST clientPredictTick into
        // `snap.tick`, so the packet's most-recent input carries the
        // last physics tick's number.  Prior k_inputRedundancy
        // ticks are pulled from `Client::inputRing_` (which the
        // sendInputSnapshot path appends to internally).
        registry.view<LocalPlayer, InputSnapshot>().each(
            [this, throwGrenadeThisFrame, grenadeCycleNextThisFrame, grenadeCyclePrevThisFrame, emoteRequestThisFrame](
                InputSnapshot& snap) {
                snap.throwGrenade = throwGrenadeThisFrame;
                snap.grenadeCycleNext = grenadeCycleNextThisFrame;
                snap.grenadeCyclePrev = grenadeCyclePrevThisFrame;
                snap.emoteRequest = static_cast<std::int8_t>(emoteRequestThisFrame);
                // Local prediction: cancel the emote the moment the player moves
                // or fights, matching the server's break condition.
                if (snap.forward || snap.back || snap.left || snap.right || snap.jump || snap.crouch || snap.shooting ||
                    snap.scoped || snap.reload || snap.throwGrenade || snap.ability1 || snap.ability2)
                    localEmote_ = -1;
            });
        systems::runInputSend(registry, *client);
        registry.view<LocalPlayer, InputSnapshot>().each([](InputSnapshot& snap) {
            snap.throwGrenade = false;
            snap.grenadeCycleNext = false;
            snap.grenadeCyclePrev = false;
            snap.emoteRequest = -1;
        });

        phaseSnap(phaseStats.physicsMs);

        const std::optional<PredictedPlayerState> currentBeforeSnapshot = captureLocalPredictedState();
        if (!client->poll()) {
            SDL_Log("Game: lost connection to server; returning to main menu");
            returnToMainMenuRequested_ = true;
            serverShutdownNoticeRequested_ = true;
            return SDL_APP_CONTINUE;
        }
        phaseSnap(phaseStats.networkPollMs);
        phaseStats.snapshotApplyMs = perfSnapshotApplyMs_;
        phaseStats.snapshotApplyCount = perfSnapshotApplyCount_;

        // Phase 5b: a snapshot just applied (overwriting the local
        // player's Position with the server's authoritative value at the
        // server-acked client tick). Replay the inputs we sent since
        // then to restore the predicted state at the *current* predict
        // tick — net effect is "server-side correction folded in,
        // client-side immediate response preserved".
        const bool snapshotApplied = client->consumeSnapshotApplied();
        phaseStats.snapshotApplied = snapshotApplied ? 1u : 0u;
        if (snapshotApplied) {
            const uint32_t ackedTick = client->getServerAckedClientTick();
            phaseStats.serverAckedClientTick = ackedTick;
            phaseStats.clientPredictTick = clientPredictTick;
            if (ackedTick != 0 && clientPredictTick > ackedTick) {
                phaseStats.reconcileRequestedTicks = clientPredictTick - ackedTick;
                const std::optional<PredictedPlayerState> authoritativeAtAck = captureLocalPredictedState();
                const PredictedPlayerState* predictedAtAck = predictedStateForTick(ackedTick);
                if (authoritativeAtAck) {
                    const ReconciliationDecision decision =
                        evaluateReconciliationSkip(*authoritativeAtAck, predictedAtAck, currentBeforeSnapshot);
                    phaseStats.reconcileErrorPosition = decision.positionError;
                    phaseStats.reconcileErrorVelocity = decision.velocityError;
                    phaseStats.reconcileMissingHistory = decision.missingHistory ? 1u : 0u;
                    if (decision.skip && currentBeforeSnapshot) {
                        restoreLocalPredictedState(*currentBeforeSnapshot);
                        phaseStats.reconcileSkippedExact = 1u;
                    } else {
                        phaseStats.reconcileReplayForced = 1u;
                        const systems::ReconciliationStats reconcileStats = systems::runReconciliation(
                            registry, inputRing_, ackedTick, clientPredictTick, k_physicsDt, physics::activeWorld());
                        phaseStats.reconcileRequestedTicks = reconcileStats.requestedTicks;
                        phaseStats.reconcileReplayedTicks = reconcileStats.replayedTicks;
                        phaseStats.reconcileMissingTicks = reconcileStats.missingTicks;
                        storePredictedPlayerState(clientPredictTick);
                    }
                }
            }
        }
        phaseSnap(phaseStats.reconciliationMs);

        refreshRemotePlayerRenderables();
        phaseSnap(phaseStats.refreshPlayersMs);
        refreshRemoteProjectileRenderables();
        phaseSnap(phaseStats.refreshProjectilesMs);
        refreshRemoteRespawnRenderables();
        phaseSnap(phaseStats.refreshRespawnsMs);
        refreshDroppedWeaponRenderables();
        phaseSnap(phaseStats.refreshDroppedWeaponsMs);
        refreshRemotePowerupRenderables();
        refreshRemoteHealthPackRenderables();
        phaseSnap(phaseStats.refreshPowerupsMs);
    }

    // 5. Bail out early if there is nothing new to render
    if (!renderSeparateFromPhysics && !physicsRan)
        return SDL_APP_CONTINUE;

    // 6. Resolve camera
    glm::vec3 renderEye{0.0f, 100.0f, 0.0f};
    float renderYaw = 0.0f;
    float renderPitch = 0.0f;
    float targetRoll = 0.0f; // degrees, from PlayerVisState

    if (renderSeparateFromPhysics) {
        // Phase 5b: local player uses physics-tick alpha — its Position
        // updates every 7.8 ms (128 Hz) via client-side prediction, so
        // the lerp window matches that interval. Phase 5a's snapshot
        // alpha is used for *remote* entities (in the render-list loop
        // below) where Position only updates on snapshot arrival.
        const float alpha = std::clamp(accumulator / k_physicsDt, 0.0f, 1.0f);

        registry.view<LocalPlayer, Position, PreviousPosition, InputSnapshot, CollisionShape, PlayerVisState>().each(
            [&](const Position& pos,
                const PreviousPosition& prev,
                const InputSnapshot& input,
                const CollisionShape& shape,
                const PlayerVisState& pstate) {
                const glm::vec3 interpPos = glm::mix(prev.value, pos.value, alpha);
                const float eyeOffset = shape.halfExtents.y * 0.77f;
                // When gravity is flipped, the player's head is at the bottom
                // of the AABB (near the ceiling they walk on).
                const float eyeDir = pstate.gravityFlipped ? -1.0f : 1.0f;
                renderEye = interpPos + glm::vec3{0.0f, eyeOffset * eyeDir, 0.0f};
                renderYaw = input.yaw;
                renderPitch = input.pitch;
                // Gravity flip adds 180° roll for the upside-down view.
                targetRoll = pstate.targetCameraTilt + (pstate.gravityFlipped ? 180.0f : 0.0f);
            });
    } else {
        // Sequential mode: use post-tick state directly (no interpolation).
        registry.view<LocalPlayer, Position, InputSnapshot, CollisionShape, PlayerVisState>().each(
            [&](const Position& pos,
                const InputSnapshot& input,
                const CollisionShape& shape,
                const PlayerVisState& pstate) {
                const float eyeOffset = shape.halfExtents.y * 0.77f;
                const float eyeDir = pstate.gravityFlipped ? -1.0f : 1.0f;
                renderEye = pos.value + glm::vec3{0.0f, eyeOffset * eyeDir, 0.0f};
                renderYaw = input.yaw;
                renderPitch = input.pitch;
                targetRoll = pstate.targetCameraTilt + (pstate.gravityFlipped ? 180.0f : 0.0f);
            });
    }

    // Emote third-person camera: while the local player emotes, ease the camera
    // back and up behind them so they (and everyone watching) see the full-body
    // emote. The blend makes the swing-out / swing-back smooth.
    {
        const float target = (localEmote_ >= 0) ? 1.0f : 0.0f;
        const float rate = 6.0f; // ~0.17 s to fully swing.
        emoteCamBlend_ += (target - emoteCamBlend_) * std::min(1.0f, rate * frameTime);
        if (emoteCamBlend_ < 0.001f)
            emoteCamBlend_ = 0.0f;
        if (emoteCamBlend_ > 0.001f) {
            const float cosPitch = std::cos(renderPitch);
            const glm::vec3 forward{
                std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
            constexpr float k_emoteCamDistance = 260.0f; // units behind the eye.
            constexpr float k_emoteCamHeight = 70.0f;    // units above the eye.
            renderEye += emoteCamBlend_ * (-forward * k_emoteCamDistance + glm::vec3{0.0f, k_emoteCamHeight, 0.0f});
        }
    }

    // Killcam: while dead and awaiting respawn, freeze the eye at the death
    // position and rotate to keep the killer centered, framed by a red box (the
    // box itself is drawn by the HUD). Inactive if the killer can't be located
    // (disconnected, world/fall death, or a suicide).
    {
        bool killcamActive = false;
        entt::entity localPlayerEnt = entt::null;
        registry.view<LocalPlayer>().each([&](entt::entity e) { localPlayerEnt = e; });

        if (localPlayerEnt != entt::null && registry.all_of<DeathInfo, RespawnTimer>(localPlayerEnt)) {
            const auto& deathInfo = registry.get<DeathInfo>(localPlayerEnt);
            ClientId localCid{-1};
            if (const auto* lc = registry.try_get<ClientId>(localPlayerEnt))
                localCid = *lc;

            // Don't track a self-inflicted death (fall/suicide).
            if (!(localCid.value != -1 && deathInfo.killerId == localCid)) {
                entt::entity killer = entt::null;
                registry.view<ClientId, Position, CollisionShape>().each(
                    [&](entt::entity e, const ClientId& cid, const Position&, const CollisionShape&) {
                        if (cid == deathInfo.killerId)
                            killer = e;
                    });

                if (killer != entt::null) {
                    const glm::vec3 killerPos = registry.get<Position>(killer).value;
                    const glm::vec3 killerHalf = registry.get<CollisionShape>(killer).halfExtents;
                    // Aim at the killer's torso (a little below AABB center reads
                    // best for a humanoid whose Position is the capsule centre).
                    const glm::vec3 killerCenter = killerPos + glm::vec3{0.0f, killerHalf.y * 0.1f, 0.0f};

                    // Lock the eye at the death position on the activation edge
                    // (killcamActive_ still holds the previous frame's value here).
                    if (!killcamActive_) {
                        killcamEye_ = renderEye;
                        killcamYaw_ = renderYaw;
                        killcamPitch_ = renderPitch;
                    }

                    const glm::vec3 toKiller = killerCenter - killcamEye_;
                    if (glm::length(toKiller) > 1.0f) {
                        const glm::vec3 dir = glm::normalize(toKiller);
                        // Matches the camera basis: fwd = {sinYaw*cosPitch,
                        // -sinPitch, cosYaw*cosPitch}.
                        const float targetYaw = std::atan2(dir.x, dir.z);
                        const float targetPitch = std::asin(std::clamp(-dir.y, -1.0f, 1.0f));

                        const float blend = std::min(1.0f, 8.0f * frameTime);
                        killcamYaw_ += std::remainder(targetYaw - killcamYaw_, glm::two_pi<float>()) * blend;
                        killcamPitch_ += (targetPitch - killcamPitch_) * blend;

                        renderEye = killcamEye_;
                        renderYaw = killcamYaw_;
                        renderPitch = killcamPitch_;
                        targetRoll = 0.0f;

                        killcamKillerCenter_ = killerPos;
                        killcamKillerHalf_ = killerHalf;
                        killcamKillerEntity_ = killer;
                        char nameBuf[32];
                        killcamKillerName_ = lookupPlayerName(registry, deathInfo.killerId, nameBuf, sizeof(nameBuf));
                        killcamActive = true;
                    }
                }
            }
        }
        killcamActive_ = killcamActive;
        if (!killcamActive)
            killcamKillerEntity_ = entt::null;
    }

    phaseSnap(phaseStats.cameraResolveMs);
    phaseStats.cameraMs = phaseStats.cameraResolveMs;

    // Local weapon VFX — fires continuously while the fire input is held,
    // respecting cooldown.  Mirrors the server's fire rate so the local
    // player sees tracers/impacts at the same cadence as the server.
    // Beam weapons (EnergyGun) are driven by BeamState from the registry,
    // so they skip per-shot VFX here.
    {
        const WeaponConfig& wpnCfg = getWeaponConfig(currentEquippedType_);

        // Skip beam weapons (driven by BeamState). Charge VFX still arrive
        // from the server, but the local fire sound is predicted here so ADS
        // shots always produce an immediate cue.
        if (!wpnCfg.isBeam) {
            localFireCooldown_ = std::max(0.0f, localFireCooldown_ - frameTime);

            // Read fire intent from the local player's InputSnapshot rather
            // than polling the mouse directly: snap.shooting is the merged
            // bus that the input samplers (kbm + gamepad) all OR into, so
            // every fire source — LMB, gamepad RT, future bindings — drives
            // local VFX/SFX uniformly.  Still gate on mouseCaptured so a
            // stale flag (player held trigger when alt-tabbing into the
            // debug menu) doesn't keep firing while no input is being sampled.
            bool shooting = false;
            entt::entity localShooter = entt::null;
            if (mouseCaptured) {
                registry.view<LocalPlayer, InputSnapshot>().each([&](entt::entity entity, const InputSnapshot& snap) {
                    shooting = snap.shooting;
                    localShooter = entity;
                });
            }

            // Grenade throw locks out fire VFX for its wind-up, exactly like reload.
            bool grenadeThrowActive = false;
            registry.view<LocalPlayer, GrenadeState>().each([&](const GrenadeState& grenades) {
                const float elapsed = getGrenadeConfig(grenades.selected).throwCooldown - grenades.cooldown;
                grenadeThrowActive = grenades.cooldown > 0.0f && elapsed >= 0.0f && elapsed < kGrenadeThrowAnimTime;
            });

            // Check ammo — don't spawn VFX if the magazine is empty or mid-reload/throw.
            bool hasAmmo = false;
            registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
                const GunInstance& gun = getEquippedGun(ws);
                hasAmmo = gun.currentMagAmmo > 0 && !gun.isReloading && !grenadeThrowActive;
                if (gun.recoilHeat == 0.0f) {
                    localRecoilHeat_ = 0.0f;
                    recoilIdleTime_ = 0.0f;
                }
            });
            if (!shooting) {
                recoilIdleTime_ += frameTime;
                if (recoilIdleTime_ > 0.2f) {
                    localRecoilHeat_ -= wpnCfg.recoilRecovery * frameTime;
                    if (localRecoilHeat_ <= float(wpnCfg.recoilFreeShots))
                        localRecoilHeat_ = 0.0f;
                }
            } else {
                recoilIdleTime_ = 0.0f;
            }

            // Phase F third-person recoil hook. Called from both fire paths
            // (charge + hitscan) to push an additive pitch impulse onto the
            // local player's spine via the animator. The impulse magnitude is
            // per-weapon-class (lighter weapons kick less). Remote players see
            // their own animator's recoil because the shot event is replicated
            // through WeaponFiredEvent / the dispatcher, but for now we only
            // wire the local player — remote recoil can be added later by
            // listening to WeaponFiredEvent.
            auto triggerLocalRecoil = [&]() {
                if (localShooter == entt::null || !registry.valid(localShooter))
                    return;
                auto* ac = registry.try_get<AnimatedCharacter>(localShooter);
                if (ac == nullptr || !ac->animator)
                    return;
                if (!isRenderableGunType(currentEquippedType_))
                    return;
                const auto& tpRecoil = tpWeaponParams_[static_cast<int>(currentEquippedType_)];
                ac->animator->applyRecoilImpulse(tpRecoil.recoilKickRad);
            };

            if (wpnCfg.isCharge) {
                if (shooting && localFireCooldown_ <= 0.0f && hasAmmo) {
                    localFireCooldown_ = wpnCfg.fireCooldown;
                    if (sfxSystem.isInitialized()) {
                        const audio::AudioObjectId object = audioObjectForEntity(localShooter);
                        sfxSystem.setAudioObjectTransform(object, cachedEye_);
                        sfxSystem.postLocalAudioEvent("weapon.railgun.fire", object, 1.0f);
                    }

                    const RecoilParams& rp = getRecoilParams(currentEquippedType_);
                    recoilPitch_ += rp.pitchKick;
                    recoilPushBack_ += rp.pushBack;
                    recoilRoll_ += rp.rollKick * ((std::rand() % 2 == 0) ? 1.0f : -1.0f);
                    triggerLocalRecoil();
                }
            } else if (shooting && localFireCooldown_ <= 0.0f && hasAmmo) {
                localFireCooldown_ = wpnCfg.fireCooldown;

                const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
                // When gravity-flipped the 180° camera roll swaps screen-
                // left/right and screen-up/down in world space.  Negate both
                // offsets so the tracer still originates at screen bottom-right
                // (where the viewmodel muzzle is).
                const float hSign = localGravFlipped ? -1.0f : 1.0f;
                glm::vec3 hip =
                    cachedEye_ + right * (hSign * 15.f) - glm::vec3{0, 1, 0} * (hSign * 8.f) + cachedCamFwd_ * 5.f;
                if (cachedMuzzleValid_) {
                    hip = cachedMuzzleWorld_;
                }

                // Raycast world geometry for tracer endpoint.  Impact effects
                // (sparks / blood / bullet holes) are NOT spawned here — they
                // come from the server's authoritative NetParticleEvent so that
                // player hits get the correct surface type and normal.
                const auto worldHit = physics::raycastWorld(cachedEye_, cachedCamFwd_, physics::activeWorld());
                const glm::vec3 hitPos = worldHit.hit ? worldHit.point : (cachedEye_ + cachedCamFwd_ * 5000.f);

                // Dispatch weapon-fired event for any listeners
                WeaponFiredEvent wfe;
                wfe.shooter = localShooter;
                wfe.type = currentEquippedType_;
                wfe.origin = hip;
                wfe.direction = cachedCamFwd_;
                wfe.isHitscan = true;
                wfe.localPlayer = true;
                wfe.hitPos = hitPos;
                dispatcher.enqueue(wfe);

                // Spawn tracer from hip toward the crosshair hit point (not along
                // cachedCamFwd_ — the hip is offset from the eye, so the direction
                // to the hit point differs slightly from the camera forward).
                const glm::vec3 hipToHit = hitPos - hip;
                const float hipHitDist = glm::length(hipToHit);
                const glm::vec3 hipDir = (hipHitDist > 0.1f) ? hipToHit / hipHitDist : cachedCamFwd_;
                particleSystem.spawnBulletTracer(hip, hipDir, hipHitDist);

                // Muzzle-flash point light for the local player's own shot.
                // Spawned here (not in onRawParticleEvent) because the server
                // echo of our own non-charge fire is skipped for instant local
                // feedback — see the early return in onRawParticleEvent. Origin
                // is 10 units ahead of the right palm along the view direction.
                spawnMuzzleFlashLight(muzzleFlashOrigin(hip));

                // Visual recoil kick (viewmodel-only).
                // Third-person recoil happens via the WeaponFiredEvent
                // dispatcher subscriber (`Game::onWeaponFired`) so the same
                // path handles future remote-shooter events without local
                // double-application.
                const RecoilParams& rp = getRecoilParams(currentEquippedType_);
                recoilPitch_ += rp.pitchKick;
                recoilPushBack_ += rp.pushBack;
                recoilRoll_ += rp.rollKick * ((std::rand() % 2 == 0) ? 1.0f : -1.0f);

                // Camera recoil.
                //
                // PATTERN PATH (preferred when wpnCfg.recoilPatternScale > 0):
                //   Closed-form R-301 pattern is defined over n in [1, 28].
                //   We resample across the magazine — shot 1 → n=1, shot magSize → n=28
                //   — so a full mag walks the entire pattern domain at finer
                //   granularity than the original 28-shot Apex pattern. Per-shot
                //   delta = sample(s) - sample(s-1), where sample(s) = (H(n), V(n))
                //   at the s-th resampled point.
                //
                // LEGACY FORMULA PATH (only if recoilPatternScale == 0 and
                //   recoilPitchPerShot > 0): the old sin+exp formula.
                const bool usePattern = wpnCfg.recoilPatternScale > 0.0f && wpnCfg.magazineSize > 1;
                const bool useLegacy = !usePattern && wpnCfg.recoilPitchPerShot > 0.0f;

                if ((usePattern || useLegacy) && localShooter != entt::null) {
                    float pitchKick = 0.0f;
                    float yawKick = 0.0f;

                    if (usePattern) {
                        const int magShots = wpnCfg.magazineSize;
                        const float scale = wpnCfg.recoilPatternScale * recoilPatternScaleMultiplier_;
                        const auto sampleAt = [&](int s) -> glm::vec2 {
                            if (s <= 0)
                                return {0.0f, 0.0f};
                            const int clamped = std::min(s, magShots);
                            const float n =
                                1.0f + static_cast<float>(clamped - 1) * 27.0f / static_cast<float>(magShots - 1);
                            return {recoilPatternH_R301(n), recoilPatternV_R301(n)};
                        };

                        const int shotNum = static_cast<int>(localRecoilHeat_) + 1;
                        const glm::vec2 delta = sampleAt(shotNum) - sampleAt(shotNum - 1);
                        yawKick = delta.x * scale;   // H positive = right
                        pitchKick = delta.y * scale; // V positive = up
                    } else {
                        // Legacy sin+exp formula.
                        const float heat = std::max(0.0f, localRecoilHeat_ - float(wpnCfg.recoilFreeShots));
                        if (heat > 0.0f) {
                            const float dk = 1.5f / wpnCfg.recoilRampShots;
                            const float ok = 2.0f / wpnCfg.recoilRampShots;
                            const float stem = wpnCfg.recoilRampShots * 0.5f;
                            const float pHeat = std::max(0.0f, heat - stem);
                            pitchKick = wpnCfg.recoilPitchPerShot * std::exp(-dk * pHeat);

                            const float hNow = std::max(0.0f, heat - stem);
                            const float hPrev = std::max(0.0f, heat - 1.0f - stem);
                            const float oNow = 1.0f - std::exp(-ok * hNow);
                            const float oPrev = 1.0f - std::exp(-ok * hPrev);
                            yawKick = wpnCfg.recoilYawPerShot *
                                      (oNow * std::sin(hNow * 0.35f) - oPrev * std::sin(hPrev * 0.35f));
                        }
                    }

                    if (pitchKick != 0.0f || yawKick != 0.0f) {
                        if (useSpringCameraRecoil_) {
                            cameraRecoilTargetPitch_ -= pitchKick;
                            cameraRecoilTargetYaw_ += yawKick;
                        } else {
                            auto& snap = registry.get<InputSnapshot>(localShooter);
                            applyRecoilAimDelta(snap, -pitchKick, yawKick);
                        }
                    }
                    localRecoilHeat_ += 1.0f;
                }
            }
        }
    }
    phaseSnap(phaseStats.localVfxMs);

    // Flush dispatcher events (weapon fired, impact, explosion)
    dispatcher.update();
    phaseSnap(phaseStats.dispatchMs);

    // Drive fresh molotov ground-fire VFX from replicated FireField entities.
    registry.view<FireField>().each([&](entt::entity e, const FireField& field) {
        particleSystem.driveGroundFire(e, field.position, field.radius, field.remaining, field.remaining);
    });

    // Update particle system (render-rate, not physics-rate)
    particleSystem.update(frameTime, renderer->getCamera(), registry);
    phaseSnap(phaseStats.particlesMs);

    audio::ListenerState audioListener;
    audioListener.position = cachedEye_;
    audioListener.forward = cachedCamFwd_;
    audioListener.up = cachedGravFlipped_ ? glm::vec3{0.0f, -1.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    registry.view<LocalPlayer, Velocity>().each(
        [&](const Velocity& velocity) { audioListener.velocity = velocity.value; });
    sfxSystem.setListener(audioListener);

    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(nullptr, nullptr);
    const bool pttHeld =
        userSettings && (userSettings->inputBindings.pressed(Action::PushToTalk, keyboard, mouseButtons) ||
                         userSettings->inputBindings.controllerPressed(Action::PushToTalk, activeGamepad_));
    const bool imguiTextInput = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput;
    voiceChat_.setPushToTalk(pttHeld && mouseCaptured && !chatOpen_ && !imguiTextInput);
    voiceChat_.update(frameTime, *client, registry, sfxSystem);

    // Update SFX system: retire finished voices, tick cooldowns, detect state changes.
    sfxSystem.update(frameTime, registry);

    // Weapon-specific sound state (charge rifle load, beam loop).
    if (sfxSystem.isInitialized()) {
        // Charge rifle: play load sound once when charging starts.
        bool isChargingNow = false;
        registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
            const GunInstance& gun = getEquippedGun(ws);
            if (getWeaponConfig(gun.type).isCharge && gun.chargeTime > 0.0f)
                isChargingNow = true;
        });
        if (isChargingNow && !wasChargingRailgun_)
            sfxSystem.postAudioEvent("weapon.railgun.charge_start");
        wasChargingRailgun_ = isChargingNow;

        // Energy beam: play/stop loop sound on beam active transitions.
        bool isBeamNow = false;
        registry.view<LocalPlayer, BeamState>().each([&](const BeamState& beam) { isBeamNow = beam.active; });
        if (isBeamNow && !wasBeamActive_)
            beamLoopHandle_ = sfxSystem.postAudioEvent("weapon.energy.loop", audio::kGlobalObject, 1.0f);
        if (isBeamNow && beamLoopHandle_ != SfxSystem::kInvalidSource)
            sfxSystem.updateSource(beamLoopHandle_, cachedEye_, audioListener.velocity, 0.55f);
        if (!isBeamNow && wasBeamActive_) {
            sfxSystem.stopSource(beamLoopHandle_);
            beamLoopHandle_ = SfxSystem::kInvalidSource;
        }
        wasBeamActive_ = isBeamNow;

        // Beam hitmarker: client-side raycast against player hitboxes while firing.
        // Note: Player component is not synced to clients, so we raycast against
        // HitboxInstance directly (skipping the local player entity).
        if (isBeamNow) {
            registry.view<LocalPlayer, BeamState, InputSnapshot, Position, CollisionShape, PlayerVisState>().each(
                [&](entt::entity localE,
                    const BeamState&,
                    const InputSnapshot& inp,
                    const Position& pos,
                    const CollisionShape& shape,
                    const PlayerVisState& pvis) {
                    const float beamEyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
                    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f * beamEyeDir, 0.0f};
                    const float cp = std::cos(inp.pitch);
                    const glm::vec3 dir{std::sin(inp.yaw) * cp, -std::sin(inp.pitch), std::cos(inp.yaw) * cp};

                    physics::HitboxHit bestHit;
                    bestHit.distance = 5000.0f;
                    registry.view<Position, CollisionShape, HitboxInstance>().each([&](entt::entity target,
                                                                                       const Position& tPos,
                                                                                       const CollisionShape& tShape,
                                                                                       const HitboxInstance& hb) {
                        if (target == localE)
                            return;
                        const physics::WorldAABB bounds{tPos.value - tShape.halfExtents,
                                                        tPos.value + tShape.halfExtents};
                        float aabbDist = bestHit.distance;
                        glm::vec3 aabbN{0.0f};
                        if (!physics::raycastAABB(eye, dir, bounds, bestHit.distance, aabbDist, aabbN))
                            return;
                        for (const auto& cap : hb.capsules) {
                            float dist = bestHit.distance;
                            glm::vec3 n{0.0f};
                            if (!physics::raycastCapsule(
                                    eye, dir, cap.pointA, cap.pointB, cap.radius, bestHit.distance, dist, n))
                                continue;
                            bestHit.hit = true;
                            bestHit.distance = dist;
                            bestHit.point = eye + dir * dist;
                            bestHit.normal = n;
                            bestHit.region = cap.region;
                            bestHit.entity = target;
                        }
                    });

                    if (bestHit.hit) {
                        hitmarkerTimer_ = 0.10f; // short pulse — refreshed every frame while hitting
                        hitmarkerIsHeadshot_ = (bestHit.region == BodyRegion::Head);
                    }
                });
        }
    }
    phaseSnap(phaseStats.audioMs);

    // Draw persistent HUD text each frame
    // particleSystem.drawScreenText({10.f, 10.f}, "HP 100", {0.9f, 1.f, 0.9f, 1.f}, 22.f);

    // Speedometer HUD
    // Shows km/h with a horizontal bar that fills with speed.
    // 1 Quake unit ≈ 1 inch = 0.0254 m. Speed in u/s → km/h:
    //   km/h = (u/s) * 0.0254 * 3.6 = u/s * 0.09144
    {
        float playerSpeed = 0.0f;
        registry.view<LocalPlayer, Velocity>().each(
            [&](const Velocity& pvel) { playerSpeed = glm::length(pvel.value); });

        const float k_kmh = playerSpeed * 0.09144f;
        const float k_maxKmh = 120.0f; // bar fills fully at this speed

        // Speed number (bottom-right area of screen).
        char speedText[32];
        std::snprintf(speedText, sizeof(speedText), "%.0f km/h", static_cast<double>(k_kmh));
        // particleSystem.drawScreenText({10.f, 38.f}, speedText, {0.8f, 0.9f, 1.0f, 1.0f}, 18.f);

        // Speed bar: use block characters to draw a filled bar.
        const float k_fraction = std::clamp(k_kmh / k_maxKmh, 0.0f, 1.0f);
        const int k_barLen = static_cast<int>(k_fraction * 20.0f);

        // Color: green → yellow → red as speed increases.
        glm::vec4 barColor;
        if (k_fraction < 0.5f)
            barColor =
                glm::mix(glm::vec4(0.3f, 0.9f, 0.4f, 0.8f), glm::vec4(1.0f, 0.9f, 0.2f, 0.8f), k_fraction * 2.0f);
        else
            barColor = glm::mix(
                glm::vec4(1.0f, 0.9f, 0.2f, 0.8f), glm::vec4(1.0f, 0.25f, 0.15f, 0.9f), (k_fraction - 0.5f) * 2.0f);

        // Build bar string with block chars.
        char barStr[64] = {};
        for (int i = 0; i < k_barLen && i < 20; ++i)
            barStr[i] = '|';

        // Background (empty portion).
        char bgStr[64] = {};
        for (int i = 0; i < 20 - k_barLen && i < 20; ++i)
            bgStr[i] = '.';

        if (k_barLen > 0) {
            // particleSystem.drawScreenText({10.f, 58.f}, barStr, barColor, 16.f);
        }
        if (k_barLen < 20) {
            // particleSystem.drawScreenText(
            //     {10.f + static_cast<float>(k_barLen) * 8.f, 58.f}, bgStr, {0.3f, 0.3f, 0.3f, 0.4f}, 16.f);
        }
    }

    // Grapple cable visual
    registry.view<LocalPlayer, PlayerVisState>().each([&](const PlayerVisState& pstate) {
        if (pstate.grappleActive) {
            (void)pstate;
            // particleSystem.spawnHitscanBeam(hand, pstate.grapplePoint, WeaponType::EnergyRifle);
        }
    });

    // Compute camera forward and cache for event() key shortcuts
    {
        const float cosPitch = std::cos(renderPitch);
        cachedCamFwd_ =
            glm::vec3{std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
        cachedEye_ = renderEye;
        cachedGravFlipped_ = localGravFlipped;
    }

    // PR-19: unified pre-render interpolation pass.  Walks every
    // non-local entity with an `InterpolationBuffer` and overwrites
    // its `Position.value` + `InputSnapshot.yaw` with the buffer-
    // sampled values at `now − N × snapshotInterval`.  Every
    // downstream visual consumer (renderer, tracers, ribbon trails,
    // smoke emitters, beam endpoints, sfx) reads `pos.value`
    // directly, so they all align on a single interpolated
    // source-of-truth — no body-vs-tracer mismatch.  No-op when
    // `GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS=0`.
    client->applyInterpolatedTransforms(registry);
    phaseSnap(phaseStats.interpolationMs);

    // Per-frame storage that crosses the animation block → entity-render block boundary:
    // populated inside the animation candidates writeback (after IK) and consumed when
    // entityCmds is built below. The weapon is parented to the right-hand bone, so its
    // world transform can only be computed once the animator has finished running.
    std::vector<EntityRenderCmd> candidateWeaponCmds;
    std::unordered_set<entt::entity> entitiesWithCandidateWeapon;

    // Update skeletal animation + build the renderer's per-frame skinned
    // palette + instance arrays (perf Phase 1B).
    //
    // For each animated entity we:
    //   1. sample + blend its animation,
    //   2. populate JointMatrices for the hitbox system,
    //   3. append `numJoints` skin matrices to the flat palette,
    //   4. append one entry to the instance buffer (world transform +
    //      paletteBase = instanceIdx * numJoints).
    //
    // The renderer then issues one instanced draw per rig mesh per pass.  No
    // CPU skinning, no per-entity vertex re-upload, no per-entity draws.
    {
        const int numJoints = charRig_.numJoints();
        const float snapshotAlpha = client->getSnapshotAlpha();
        // PR-11: render-time = now − N × snapshotInterval (default 2 ticks
        // ≈ 62.5 ms at 32 Hz).  0 means "no buffered playback yet" — the
        // renderer falls back to the Phase-5a (prev, cur, alpha) lerp
        // through `entity_interpolation::sample`'s fallback path.
        const Uint64 interpRenderNs = client->getInterpolationRenderTimeNs();
        constexpr float k_animationTick = 1.0f / 30.0f;

        // ─── Phase 1: sequential prepass ────────────────────────────────────
        // Walk the registry view ONCE on the main thread (entt views aren't
        // thread-safe to iterate concurrently).  For each animated character
        // build an AnimCandidate that captures everything needed by the
        // parallel pass below, including pre-built AnimationInputs and a
        // pre-computed world transform.  Out-of-view characters with no
        // shadow contribution are dropped here.
        struct AnimCandidate
        {
            entt::entity entity{};
            AnimatedCharacter* ac = nullptr;
            AnimationInputs ai;
            glm::vec3 audioPosition{0.0f};
            glm::mat4 worldTransform{1.0f};
            glm::vec4 tint{1.0f, 1.0f, 1.0f, 0.0f}; ///< rgb=color, a=blend factor (0=no tint).
            bool sampleThisFrame = false;           ///< call animator->update()
            bool drawThisFrame = false;             ///< write to instance/palette slots
            bool isLocal = false;
            uint32_t slot = 0;                      ///< index into skinnedInstances + base into bonePalette
            // Third-person weapon hold (FK rewrite): the weapon is a rigid child
            // of Spine2 and both arms are FK-posed by `holdPose`. The worker
            // applies the pose to the animator and derives `weaponWorld` from the
            // post-spine-bend Spine2 model matrix.
            bool hasWeapon = false;
            int weaponModelIdx = -1;
            glm::mat4 weaponWorld{1.0f};
            float weaponScale = 1.0f;
            bool hasHoldPose = false;  ///< Apply `holdPose` to the animator this frame.
            WeaponHoldPose holdPose{}; ///< Per-weapon spine-relative offset/rotation + FK arm pose.
            float holdWeight = 1.0f;   ///< Weapon-swap fade weight for the arm FK (0→1).
        };
        // Plain function-local (NOT thread_local — workers must see the
        // main thread's vector through the lambda capture).  Reserved up
        // front to avoid reallocs across frames.
        std::vector<AnimCandidate> candidates;
        candidates.reserve(128);
        const auto ragdollPoses = collectClientRagdollPoses(registry);

        // Frustum culling for skinned characters now lives in SkinnedRenderer:
        // it sphere-tests every submitted instance against the camera frustum
        // planes using the rig's real (mesh-centred) bounding sphere.  We hand
        // it ALL non-dead players below and let it pick the visible subset —
        // see SkinnedRenderer::setFrame.  (Doing it here off `pos.value` culled
        // against the sim position, not the offset rendered mesh, which popped
        // characters out at the screen edge.)

        // Detect movement-state transitions for SFX (landing, slide, abilities, respawn).
        // Runs over ALL PlayerVisState entities — dead, off-screen, and remote alike —
        // so a respawn or slide-stop is never missed because the entity was culled.
        if (sfxSystem.isInitialized()) {
            std::unordered_set<entt::entity> alive;
            alive.reserve(playerSfxState_.size() + 4);
            registry.view<PlayerVisState, Position, Velocity>().each(
                [&](entt::entity e, const PlayerVisState& vis, const Position& pos, const Velocity& vel) {
                    alive.insert(e);
                    const bool isLocal = registry.all_of<LocalPlayer>(e);
                    const auto* abil = registry.try_get<AbilityState>(e);
                    PlayerSfxState& tracked = playerSfxState_[e];

                    const audio::AudioObjectId object = audioObjectForEntity(e);
                    sfxSystem.setAudioObjectTransform(object, pos.value, vel.value);
                    const auto post = [&](std::string_view eventName, float gain) {
                        if (isLocal)
                            sfxSystem.postLocalAudioEvent(eventName, object, gain);
                        else
                            sfxSystem.postAudioEvent(eventName, object, gain);
                    };

                    const int slidingMode = static_cast<int>(MoveMode::Sliding);

                    if (!tracked.initialized) {
                        tracked.grounded = vis.grounded;
                        tracked.isDead = vis.isDead;
                        tracked.moveMode = static_cast<int>(vis.moveMode);
                        tracked.gravityFlipped = vis.gravityFlipped;
                        tracked.grappleActive = vis.grappleActive;
                        if (abil != nullptr) {
                            tracked.primaryCooldown = abil->primaryCooldown;
                            tracked.secondaryCooldown = abil->secondaryCooldown;
                        }
                        tracked.initialized = true;
                        return;
                    }

                    const int newMode = static_cast<int>(vis.moveMode);

                    // Landing: airborne → grounded. Skip dead/respawn-frame and
                    // slide entry (slide has its own cue).
                    if (!tracked.grounded && vis.grounded && !tracked.isDead && !vis.isDead && newMode != slidingMode) {
                        const float vy = std::abs(vel.value.y);
                        const float gain = std::clamp(0.55f + vy / 1400.0f, 0.55f, 1.0f);
                        post("player.land", gain);
                    }

                    // Slide entry / exit: track MoveMode::Sliding edge.
                    if (tracked.moveMode != slidingMode && newMode == slidingMode) {
                        if (tracked.slideLoopHandle != SfxSystem::kInvalidSource) {
                            sfxSystem.stopSource(tracked.slideLoopHandle);
                            tracked.slideLoopHandle = SfxSystem::kInvalidSource;
                        }
                        tracked.slideLoopHandle = sfxSystem.startLoop(SfxId::Slide, !isLocal, pos.value, 0.9f, 1.4f);
                    } else if (tracked.moveMode == slidingMode && newMode != slidingMode) {
                        if (tracked.slideLoopHandle != SfxSystem::kInvalidSource) {
                            sfxSystem.stopSource(tracked.slideLoopHandle);
                            tracked.slideLoopHandle = SfxSystem::kInvalidSource;
                        }
                    } else if (tracked.slideLoopHandle != SfxSystem::kInvalidSource && newMode == slidingMode) {
                        sfxSystem.updateSource(tracked.slideLoopHandle, pos.value, vel.value, 0.9f);
                    }

                    // Gravity flip ability: gravityFlipped toggled this frame.
                    if (tracked.gravityFlipped != vis.gravityFlipped)
                        post("ability.gravity", 1.0f);

                    // Grapple ability: rising edge of grappleActive.
                    if (!tracked.grappleActive && vis.grappleActive)
                        post("ability.grapple", 1.0f);

                    // Ability cooldown rising edge → activation. Grapple is covered
                    // by the grappleActive edge above, Gravity by the flip edge.
                    if (abil != nullptr) {
                        const auto rose = [](float prev, float cur) { return cur > prev + 0.05f; };
                        const bool primaryFired = rose(tracked.primaryCooldown, abil->primaryCooldown);
                        const bool secondaryFired = rose(tracked.secondaryCooldown, abil->secondaryCooldown);
                        if (primaryFired && abil->primary == AbilityType::Dash)
                            post("ability.dash", 1.0f);
                        if (secondaryFired && abil->secondary == AbilityType::Recall)
                            post("ability.recall", 1.0f);
                    }

                    // Respawn: dead → alive.
                    if (tracked.isDead && !vis.isDead)
                        post("player.respawn", 1.0f);

                    tracked.grounded = vis.grounded;
                    tracked.isDead = vis.isDead;
                    tracked.moveMode = newMode;
                    tracked.gravityFlipped = vis.gravityFlipped;
                    tracked.grappleActive = vis.grappleActive;
                    if (abil != nullptr) {
                        tracked.primaryCooldown = abil->primaryCooldown;
                        tracked.secondaryCooldown = abil->secondaryCooldown;
                    }
                });

            // Reap tracking entries for entities that vanished (disconnect / scene change).
            for (auto it = playerSfxState_.begin(); it != playerSfxState_.end();) {
                if (alive.count(it->first) == 0) {
                    if (it->second.slideLoopHandle != SfxSystem::kInvalidSource)
                        sfxSystem.stopSource(it->second.slideLoopHandle);
                    it = playerSfxState_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        uint32_t drawSlot = 0;
        registry.view<AnimatedCharacter, Position, Velocity, PlayerVisState, InputSnapshot>().each(
            [&](entt::entity e,
                AnimatedCharacter& ac,
                const Position& pos,
                const Velocity& vel,
                const PlayerVisState& ps,
                const InputSnapshot& inp) {
                if (!ac.animator)
                    return;
                const bool isLocal = registry.all_of<LocalPlayer>(e);

                // Dead players are rendered from replicated ragdoll bones below.
                if (ps.isDead)
                    return;

                AnimCandidate c;
                c.entity = e;
                c.ac = &ac;
                c.isLocal = isLocal;
                c.audioPosition = pos.value;

                if constexpr (player_colors::k_enabled) {
                    if (const auto* pc = registry.try_get<PlayerColor>(e); pc != nullptr) {
                        c.tint = glm::vec4(pc->rgb, player_colors::k_blendFactor);
                    }
                }

                c.ai.velocityWorld = vel.value;
                c.ai.yawRad = inp.yaw;
                c.ai.pitchRad = inp.pitch;
                c.ai.dtSec = frameTime;
                c.ai.grounded = ps.grounded;
                c.ai.sprinting = ps.sprinting;
                c.ai.crouching = ps.crouching;
                c.ai.moveMode = static_cast<int>(ps.moveMode);
                c.ai.wallRunSide = static_cast<int>(ps.wallRunSide);
                // Local player's emote is predicted client-side for instant
                // feedback; remote players render their emote straight from the
                // server's AnimSnapshot (renderFromServer), so only drive the
                // override clip here for the local player.
                if (isLocal && localEmote_ >= 0)
                    c.ai.emoteClip = static_cast<int>(emoteClipForIndex(localEmote_));
                // Phase F per-weapon-class procedural multipliers. Pulled from
                // the equipped weapon's tuning row when one exists; otherwise
                // we fall through with the default values (full spine bend,
                // gentle hip lean) so unarmed/dead/dropped-weapon characters
                // still animate sanely.
                if (const auto* ws2 = registry.try_get<WeaponState>(e); ws2 != nullptr) {
                    const GunInstance& gun2 = getEquippedGun(*ws2);
                    if (isRenderableGunType(gun2.type)) {
                        const auto& tp2 = tpWeaponParams_[static_cast<int>(gun2.type)];
                        c.ai.spineBendMultiplier = tp2.spineBendMultiplier;
                        c.ai.hipLeanMultiplier = tp2.hipLeanMultiplier;
                    }
                }

                // Animation tick decoupling: cap remote state-machine updates at
                // 30 Hz.  The inputs stay populated every render frame because
                // weapon IK can force a resample between ticks; head pitch must
                // not fall back to AnimationInputs' zero defaults on those frames.
                ac.animationAccumulator += frameTime;
                if (ac.animationAccumulator >= k_animationTick || isLocal) {
                    ac.animationAccumulator = std::fmod(ac.animationAccumulator, k_animationTick);
                    c.sampleThisFrame = true;
                }

                // Skip drawing the local player's own body in first-person.
                // Also draw the local body while the emote camera has swung out,
                // so the player can watch their own emote in third person.
                const bool emoteShowsLocalBody = isLocal && emoteCamBlend_ > 0.01f;
                const bool drawBody = !(!animUI_.showLocalBody && isLocal) || emoteShowsLocalBody;
                if (drawBody && numJoints > 0) {
                    c.drawThisFrame = true;
                    c.slot = drawSlot++;

                    // Build per-instance world transform.
                    //
                    // PR-19: when the buffered render-delay path is
                    // active (interpRenderNs != 0), `Client::
                    // applyInterpolatedTransforms` has ALREADY
                    // overwritten `pos.value` and `inp.yaw` with the
                    // interpolated render-time values for non-local
                    // entities.  We just read them directly — same
                    // source-of-truth as tracers / ribbon trails /
                    // smoke / beams / sfx, so no body-vs-effect
                    // misalignment.
                    //
                    // When the buffered path is OFF (env-disabled
                    // cl_interp = 0), or for the local player (which
                    // uses client-side prediction, not interp), fall
                    // back to the Phase-5a (prev, cur, alpha) lerp
                    // for snapshot-rate motion smoothness.
                    glm::vec3 renderPos = pos.value;
                    float renderYaw = inp.yaw;
                    if (!isLocal && interpRenderNs != 0) {
                        // pos.value already pre-interpolated by
                        // applyInterpolatedTransforms; no work here.
                    } else if (const auto* prev = registry.try_get<PreviousPosition>(e)) {
                        renderPos = glm::mix(prev->value, pos.value, snapshotAlpha);
                    }
                    glm::vec3 translation(0.0f);
                    glm::vec3 scale(kRigScale_);
                    glm::quat orient = glm::angleAxis(renderYaw, glm::vec3{0, 1, 0});
                    if (const auto* rend = registry.try_get<Renderable>(e)) {
                        translation = rend->translation;
                        scale = rend->scale;
                        orient = rend->orientation;
                    } else if (const auto* shape = registry.try_get<CollisionShape>(e)) {
                        translation = glm::vec3(0.0f, -shape->halfExtents.y - rigMeshMinY_ * kRigScale_, 0.0f);
                    }
                    glm::mat4 world = glm::translate(glm::mat4(1.0f), renderPos + translation);
                    world *= glm::mat4_cast(orient);
                    world = glm::scale(world, scale);
                    c.worldTransform = world;

                    if (const auto* ws = registry.try_get<WeaponState>(e); ws != nullptr) {
                        const GunInstance& gun = getEquippedGun(*ws);
                        if (isRenderableGunType(gun.type)) {
                            c.sampleThisFrame = true;
                            // FK weapon hold (rewrite): the gun is a rigid child of Spine2 and
                            // both arms are FK-posed from the per-weapon hold pose. The worker
                            // applies the pose after sampling and derives `weaponWorld` from the
                            // post-spine-bend Spine2 model matrix.
                            c.hasWeapon = true;
                            c.weaponModelIdx = weaponModelIndices_[static_cast<std::size_t>(gun.type)];
                            c.weaponWorld = glm::mat4(1.0f); // overwritten in the worker.
                            c.hasHoldPose = spine2JointIdx_ >= 0;
                            c.holdPose = weaponHoldPoses_[static_cast<std::size_t>(gun.type)];
                            c.weaponScale = c.holdPose.scale;

                            // Weapon-swap fade: ramp the arm FK weight 0 → 1 over
                            // kHoldSwapDurationSec after a weapon-type change so the arms
                            // release the old grip and re-pose onto the new weapon.
                            constexpr float kHoldSwapDurationSec = 0.15f;
                            auto& swap = gripSwapState_[e];
                            if (swap.lastType != gun.type) {
                                swap.lastType = gun.type;
                                swap.swapElapsedSec = 0.0f;
                            }
                            swap.swapElapsedSec = std::min(swap.swapElapsedSec + frameTime, 1.0f);
                            c.holdWeight = std::clamp(swap.swapElapsedSec / kHoldSwapDurationSec, 0.0f, 1.0f);
                        }
                    }
                }
                candidates.push_back(c);
            });
        if (collectPerf) {
            phaseStats.animatedCandidates = static_cast<std::uint32_t>(candidates.size());
            for (const AnimCandidate& c : candidates) {
                if (c.sampleThisFrame)
                    ++phaseStats.animatedSampled;
                if (c.drawThisFrame)
                    ++phaseStats.animatedDrawn;
            }
        }

        // ─── Phase 2: parallel ozz-sample ──────────────────────────────────
        // Updates each candidate's animator state.  Used to also build
        // per-instance bone palettes for the legacy renderer's skinned
        // pipeline; that path is gone, but the animator update is still
        // required so JointMatrices/AnimSnapshot below reflect this frame.
        if (workerPool_ && !candidates.empty()) {
            // Freeze toggle: apply once per frame, BEFORE the worker fires so
            // the flag is visible to update()/renderFromServer() across all
            // workers without a race.
            for (auto& c : candidates) {
                if (c.ac != nullptr && c.ac->animator)
                    c.ac->animator->setFrozen(tpFreezeAnimations_);
            }

            workerPool_->parallelFor(static_cast<int>(candidates.size()), [&](int begin, int end) {
                for (int i = begin; i < end; ++i) {
                    auto& c = candidates[static_cast<size_t>(i)];
                    if (c.sampleThisFrame) {
                        if (c.isLocal) {
                            c.ac->animator->update(c.ai, c.ai.dtSec);
                        } else {
                            const auto* serverAnim = registry.try_get<AnimSnapshot>(c.entity);
                            if (serverAnim != nullptr)
                                c.ac->animator->renderFromServer(*serverAnim, c.ai);
                            else
                                c.ac->animator->update(c.ai, k_animationTick);
                        }
                    }

                    // Phase F task 14 + AAA grip rework: stage IK in the
                    // worker so the left hand can target the *actual* weapon
                    // world (which depends on the right hand being IK'd
                    // first). All writes here touch only this candidate's
                    // animator state + AnimCandidate fields — safe across
                    // candidates.

                    if (!c.drawThisFrame || c.ac == nullptr || !c.ac->animator) {
                        continue;
                    }

                    // ── 1. Pose both arms (FK) for the held weapon, then
                    // recompute the skin palette. No solving — each arm bone is
                    // driven to its authored local rotation relative to the
                    // (already-bent) spine, so the hands ride rigidly with it.
                    if (c.hasHoldPose)
                        c.ac->animator->applyWeaponHoldPose(c.holdPose, c.holdWeight);

                    // ── 2. Derive the weapon world as a rigid child of Spine2:
                    // weaponWorld = entityWorld × Spine2Model × T(offset) × R(rot) × S(scale).
                    // Spine2Model already carries the procedural spine bend, so
                    // the gun follows aim pitch automatically.
                    if (c.hasWeapon && spine2JointIdx_ >= 0) {
                        const auto& joints = c.ac->animator->jointModelMatrices();
                        if (spine2JointIdx_ < static_cast<int>(joints.size())) {
                            const glm::mat4& spine2Model = joints[static_cast<size_t>(spine2JointIdx_)];
                            glm::mat4 local = glm::translate(glm::mat4(1.0f), c.holdPose.spineOffset);
                            local *= glm::mat4_cast(glm::normalize(c.holdPose.spineRotation));
                            local = glm::scale(local, glm::vec3(c.weaponScale));
                            c.weaponWorld = c.worldTransform * spine2Model * local;
                        }
                    }
                }
            });
        }

        // ─── Phase 3: sequential registry writeback ─────────────────────────
        // JointMatrices is an EnTT component; insert/update needs the main
        // thread.  Cheap loop — just copies a per-char matrix array.
        std::vector<glm::mat4> bonePalette;
        std::vector<SkinnedInstance> skinnedInstances;
        if (numJoints > 0 && drawSlot > 0) {
            bonePalette.reserve(static_cast<size_t>(drawSlot) * static_cast<size_t>(numJoints));
            skinnedInstances.reserve(drawSlot);
        }

        // Wallhack (tier-2): while the local player's reveal window is active,
        // every other player is drawn with the red chams pass (see-through-walls).
        bool localWallhackActive = false;
        registry.view<LocalPlayer, AbilityState>().each(
            [&](const AbilityState& abil) { localWallhackActive = abil.wallhackTimer > 0.0f; });

        // Phase F task 14: applyHandIkTargets + weapon-world derivation moved
        // into the worker pool above. This pass is now purely sequential
        // registry writeback — copying joint matrices to JointMatrices, pushing
        // skin palettes for the renderer.

        // Phase F task 15: aim-assist parity check. Compare the third-person
        // weapon's barrel direction (derived from the right-hand-bone-parented
        // matrix) against the gameplay aim ray (`cachedCamFwd_`). Logs a
        // single-line report every ~2 s when divergence exceeds the threshold,
        // so per-weapon grip-local offsets can be hand-tuned to align.
        aimAssistParityAccumSec_ += frameTime;
        if (aimAssistParityAccumSec_ >= 2.0f) {
            aimAssistParityAccumSec_ = 0.0f;
            for (const auto& c : candidates) {
                if (!c.isLocal || !c.hasWeapon)
                    continue;
                // The third-person weapon mesh's local "forward" is +Z in
                // mesh-local space; transform that through the just-derived
                // weaponWorld and compare against cachedCamFwd_.
                const glm::vec4 weaponFwd4 = c.weaponWorld * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
                const glm::vec3 weaponFwd = glm::normalize(glm::vec3(weaponFwd4));
                const float dot = glm::clamp(glm::dot(weaponFwd, cachedCamFwd_), -1.0f, 1.0f);
                const float divergenceRad = std::acos(dot);
                constexpr float k_aimParityThresholdRad = 0.01745f; // 1 degree.
                if (divergenceRad > k_aimParityThresholdRad) {
                    SDL_Log("[aim-parity] weapon barrel vs aim ray diverges by %.2f deg "
                            "(barrel=(%.2f,%.2f,%.2f) aim=(%.2f,%.2f,%.2f))",
                            static_cast<double>(glm::degrees(divergenceRad)),
                            static_cast<double>(weaponFwd.x),
                            static_cast<double>(weaponFwd.y),
                            static_cast<double>(weaponFwd.z),
                            static_cast<double>(cachedCamFwd_.x),
                            static_cast<double>(cachedCamFwd_.y),
                            static_cast<double>(cachedCamFwd_.z));
                }
                break;
            }
        }

        for (auto& c : candidates) {
            if (c.sampleThisFrame) {
                auto& jm = registry.get_or_emplace<JointMatrices>(c.entity);
                jm.matrices = c.ac->animator->jointModelMatrices();

                // PR-27 (netsync): mirror the animator's sampler array
                // into an `AnimSnapshot` ECS component, same pattern as
                // the server.  The shot-debug fire-detection block (a
                // few lines below) reads this off the target entity
                // when sending `SHOT_INTENT`.  Server's history ring
                // captures its OWN per-tick `AnimSnapshot`; the two
                // get compared in the shot-resolution path.
                auto& snap = registry.get_or_emplace<AnimSnapshot>(c.entity);
                const auto& samplers = c.ac->animator->samplers();
                for (std::size_t i = 0; i < AnimSnapshot::k_numSlots && i < samplers.size(); ++i) {
                    const auto& src = samplers[i];
                    const bool active = src.active && src.weight > 0.0f;
                    auto& dst = snap.slots[i];
                    dst.clipIdRaw = active ? static_cast<std::uint8_t>(src.id) : 0xFFu;
                    dst.timeRatio = active ? src.timeRatio : 0.0f;
                    dst.weight = active ? src.weight : 0.0f;
                }

                auto [phaseIt, inserted] = footstepPhases_.try_emplace(c.entity);
                if (inserted)
                    phaseIt->second.fill(-1.0f);
                const bool canStep = c.ai.grounded || c.ai.moveMode == 2;
                const float speed = glm::length(c.ai.velocityWorld);
                if (sfxSystem.isInitialized() && canStep && speed > 65.0f) {
                    float& footstepCooldown = footstepCooldowns_[c.entity];
                    footstepCooldown = std::max(0.0f, footstepCooldown - frameTime);
                    for (std::size_t i = 0; i < samplers.size() && i < phaseIt->second.size(); ++i) {
                        const ClipSampler& src = samplers[i];
                        const bool audibleSampler = src.active && src.weight > 0.22f && isFootstepClip(src.id);
                        if (!audibleSampler) {
                            phaseIt->second[i] = src.active ? src.timeRatio : -1.0f;
                            continue;
                        }
                        const float previous = phaseIt->second[i];
                        const bool leftStep = footstepMarkerCrossed(previous, src.timeRatio, 0.18f);
                        const bool rightStep = footstepMarkerCrossed(previous, src.timeRatio, 0.68f);
                        if ((leftStep || rightStep) && footstepCooldown <= 0.0f) {
                            const SfxId stepId =
                                isHeavyFootstepClip(src.id) ? SfxId::FootstepHeavy : SfxId::FootstepLight;
                            const float gain = std::clamp(0.35f + src.weight * (speed / 900.0f), 0.25f, 0.85f);
                            const glm::vec3 lateral = glm::normalize(
                                glm::cross(glm::vec3{0.0f, 1.0f, 0.0f},
                                           glm::vec3{std::sin(c.ai.yawRad), 0.0f, std::cos(c.ai.yawRad)}));
                            const float side = leftStep ? -7.0f : 7.0f;
                            const audio::AudioObjectId object = audioObjectForEntity(c.entity);
                            sfxSystem.setAudioObjectTransform(
                                object, c.audioPosition + lateral * side, c.ai.velocityWorld);
                            sfxSystem.setAudioRtpc(object,
                                                   audio::rtpcId("movement.intensity"),
                                                   stepId == SfxId::FootstepHeavy ? 1.0f : 0.0f);
                            if (c.isLocal)
                                sfxSystem.postLocalAudioEvent("footstep", object, gain);
                            else
                                sfxSystem.postAudioEvent("footstep", object, gain);
                            footstepCooldown = kMinFootstepIntervalSeconds;
                        }
                        phaseIt->second[i] = src.timeRatio;
                    }
                } else {
                    for (std::size_t i = 0; i < samplers.size() && i < phaseIt->second.size(); ++i)
                        phaseIt->second[i] = samplers[i].active ? samplers[i].timeRatio : -1.0f;
                }
            }

            if (c.drawThisFrame && c.ac != nullptr && c.ac->animator) {
                const std::vector<glm::mat4>& skinMatrices = c.ac->animator->skinMatrices();
                if (skinMatrices.size() != static_cast<size_t>(numJoints))
                    continue;

                SkinnedInstance instance;
                instance.worldTransform = c.worldTransform;
                instance.paletteBase = static_cast<uint32_t>(bonePalette.size());
                instance.tint = c.tint;
                // Flag for the red chams pass: the killcam killer, or — while the
                // local player's wallhack is active — every other player.
                const bool chamsKiller = killcamActive_ && c.entity == killcamKillerEntity_;
                const bool chamsWallhack = localWallhackActive && !c.isLocal;
                instance.materialId = (chamsKiller || chamsWallhack) ? 1u : 0u;

                bonePalette.insert(bonePalette.end(), skinMatrices.begin(), skinMatrices.end());
                skinnedInstances.push_back(instance);
            }
        }

        // Stage third-person weapon entity-render commands derived from the
        // post-IK right-hand bone matrix for every drawn animated candidate.
        // These are consumed below when entityCmds is built; tracking the entity
        // set lets the legacy remote-weapon loop skip duplicates.
        candidateWeaponCmds.reserve(candidates.size());
        for (const auto& c : candidates) {
            if (!c.hasWeapon || !c.drawThisFrame || c.weaponModelIdx < 0)
                continue;
            candidateWeaponCmds.push_back(
                EntityRenderCmd{.modelIndex = c.weaponModelIdx, .worldTransform = c.weaponWorld});
            entitiesWithCandidateWeapon.insert(c.entity);
        }

        // Spine-anchor frame visualizer for the Weapon Hold tweaker. Renders a
        // 3-axis (R/G/B) marker at the weapon's spine-relative anchor frame
        // (entityWorld × Spine2 × T(offset) × R(rot)) on REMOTE animated
        // characters — the user observes them in third person while tuning the
        // gun's placement relative to the spine bone.
        if (showWeaponHoldUI_ && holdShowDebugMarker_ && handMountDebugMarkerModelIdx_ >= 0 && spine2JointIdx_ >= 0) {
            registry.view<AnimatedCharacter, Position, InputSnapshot, WeaponState>().each([&](entt::entity e,
                                                                                              AnimatedCharacter& ac,
                                                                                              const Position& pos,
                                                                                              const InputSnapshot& inp,
                                                                                              const WeaponState& ws) {
                if (registry.all_of<LocalPlayer>(e))
                    return;
                if (!ac.animator)
                    return;
                const GunInstance& gun = getEquippedGun(ws);
                if (!isRenderableGunType(gun.type))
                    return;
                const auto& joints = ac.animator->jointModelMatrices();
                if (spine2JointIdx_ >= static_cast<int>(joints.size()))
                    return;
                const WeaponHoldPose& hold = weaponHoldPoses_[static_cast<std::size_t>(gun.type)];

                // Reproduce the entity world transform the same way the
                // candidate populator does.
                glm::mat4 worldT = glm::translate(glm::mat4(1.0f), pos.value);
                worldT *= glm::mat4_cast(glm::angleAxis(inp.yaw, glm::vec3{0.0f, 1.0f, 0.0f}));
                worldT = glm::translate(worldT, glm::vec3{0.0f, kRigVerticalOffset_, 0.0f});
                worldT = glm::scale(worldT, glm::vec3(kRigScale_));

                glm::mat4 local = glm::translate(glm::mat4(1.0f), hold.spineOffset);
                local *= glm::mat4_cast(glm::normalize(hold.spineRotation));
                const glm::mat4 anchorFrame = worldT * joints[static_cast<size_t>(spine2JointIdx_)] * local;
                const glm::vec3 originWorld(anchorFrame[3]);
                const glm::mat3 frameR(glm::normalize(glm::vec3(anchorFrame[0])),
                                       glm::normalize(glm::vec3(anchorFrame[1])),
                                       glm::normalize(glm::vec3(anchorFrame[2])));

                const auto pushMarker = [&](const glm::vec3& worldPos, float scale, const glm::vec4& tint) {
                    glm::mat4 m = glm::translate(glm::mat4(1.0f), worldPos);
                    m = glm::scale(m, glm::vec3(scale));
                    candidateWeaponCmds.push_back(EntityRenderCmd{
                        .modelIndex = handMountDebugMarkerModelIdx_, .worldTransform = m, .tint = tint});
                };

                constexpr float k_centerScale = 0.4f;
                constexpr float k_axisScale = 0.17f;
                constexpr float k_axisLen = 5.0f;
                pushMarker(originWorld, k_centerScale, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
                pushMarker(originWorld + frameR * glm::vec3(k_axisLen, 0.0f, 0.0f),
                           k_axisScale,
                           glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));
                pushMarker(originWorld + frameR * glm::vec3(0.0f, k_axisLen, 0.0f),
                           k_axisScale,
                           glm::vec4(0.3f, 1.0f, 0.3f, 1.0f));
                pushMarker(originWorld + frameR * glm::vec3(0.0f, 0.0f, k_axisLen),
                           k_axisScale,
                           glm::vec4(0.3f, 0.5f, 1.0f, 1.0f));
            });
        }

        // Death dissolve ("Thanos snap"): when a player dies, sample their
        // frozen last pose into world-space points and hand them to the particle
        // system, which crumbles them into wind-swept ash. Dead players stop
        // being animated (the alive pass skips them), so the animator's
        // skinMatrices() still hold the final alive pose. Emitted once per death.
        if (charRig_.isLoaded() && numJoints > 0) {
            registry.view<AnimatedCharacter, Position, PlayerVisState>().each(
                [&](entt::entity e, const AnimatedCharacter& ac, const Position& pos, const PlayerVisState& ps) {
                    if (!ps.isDead) {
                        dissolveSpawned_.erase(e); // alive (or respawned) — arm for next death
                        return;
                    }
                    // Only enemies dissolve; the local player never renders their own
                    // Thanos snap effect.
                    if (registry.all_of<LocalPlayer>(e))
                        return;
                    if (!ac.animator || dissolveSpawned_.count(e) > 0)
                        return;

                    const std::vector<glm::mat4>& palette = ac.animator->skinMatrices();
                    if (palette.size() != static_cast<size_t>(numJoints))
                        return;

                    // Reproduce the entity world transform (same as the renderer).
                    glm::vec3 translation(0.0f);
                    glm::vec3 scale(kRigScale_);
                    glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
                    if (const auto* rend = registry.try_get<Renderable>(e)) {
                        translation = rend->translation;
                        scale = rend->scale;
                        orient = rend->orientation;
                    } else if (const auto* shape = registry.try_get<CollisionShape>(e)) {
                        translation = glm::vec3(0.0f, -shape->halfExtents.y - rigMeshMinY_ * kRigScale_, 0.0f);
                    }
                    glm::mat4 world = glm::translate(glm::mat4(1.0f), pos.value + translation);
                    world *= glm::mat4_cast(orient);
                    world = glm::scale(world, scale);

                    // Sample a subset of the skinned mesh to ~5000 world points.
                    size_t totalVerts = 0;
                    for (const auto& mesh : charRig_.meshes())
                        totalVerts += mesh.baseVertices.size();
                    if (totalVerts == 0)
                        return;
                    constexpr size_t k_targetPoints = 5000;
                    const size_t stride = std::max<size_t>(1, totalVerts / k_targetPoints);

                    std::vector<glm::vec3> points;
                    points.reserve(k_targetPoints + 64);
                    size_t idx = 0;
                    for (const auto& mesh : charRig_.meshes()) {
                        for (size_t v = 0; v < mesh.baseVertices.size(); ++v, ++idx) {
                            if (idx % stride != 0)
                                continue;
                            const glm::vec4 p(mesh.baseVertices[v].position, 1.0f);
                            const SkinWeight& w = mesh.skinWeights[v];
                            glm::vec3 modelPos(0.0f);
                            for (int k = 0; k < 4; ++k) {
                                if (w.weights[k] <= 0.0f)
                                    continue;
                                const int b = w.boneIndices[k];
                                if (b < 0 || b >= static_cast<int>(palette.size()))
                                    continue;
                                modelPos += w.weights[k] * glm::vec3(palette[static_cast<size_t>(b)] * p);
                            }
                            points.push_back(glm::vec3(world * glm::vec4(modelPos, 1.0f)));
                        }
                    }
                    if (points.empty())
                        return;
                    particleSystem.spawnDeathDissolve(points, pos.value, glm::vec4(0.82f, 0.82f, 0.88f, 1.0f));
                    dissolveSpawned_.insert(e);
                });
        }

        if (kRagdollsEnabled && charRig_.isLoaded() && numJoints > 0) {
            registry.view<AnimatedCharacter, Position, PlayerVisState, ClientId>().each([&](entt::entity e,
                                                                                            AnimatedCharacter& ac,
                                                                                            const Position& pos,
                                                                                            const PlayerVisState& ps,
                                                                                            const ClientId& clientId) {
                if (!ps.isDead || !ac.animator)
                    return;
                if (registry.all_of<LocalPlayer>(e) && !animUI_.showLocalBody)
                    return;

                const auto poseIt = ragdollPoses.find(clientId);
                if (poseIt == ragdollPoses.end())
                    return;

                // Frustum culling for these dead-player ragdoll instances is
                // handled by SkinnedRenderer too — submit and let it cull.
                std::vector<glm::mat4> ragdollSkinMatrices = ac.animator->skinMatrices();
                if (ragdollSkinMatrices.size() != static_cast<size_t>(numJoints))
                    return;

                glm::vec3 translation(0.0f);
                glm::vec3 scale(kRigScale_);
                glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
                if (const auto* rend = registry.try_get<Renderable>(e)) {
                    translation = rend->translation;
                    scale = rend->scale;
                    orient = rend->orientation;
                } else if (const auto* shape = registry.try_get<CollisionShape>(e)) {
                    translation = glm::vec3(0.0f, -shape->halfExtents.y - rigMeshMinY_ * kRigScale_, 0.0f);
                }

                glm::mat4 world = glm::translate(glm::mat4(1.0f), pos.value + translation);
                world *= glm::mat4_cast(orient);
                world = glm::scale(world, scale);

                applyRagdollPoseToSkinPalette(ragdollSkinMatrices, charRig_, poseIt->second, world, kRigScale_);

                SkinnedInstance instance;
                instance.worldTransform = world;
                instance.paletteBase = static_cast<uint32_t>(bonePalette.size());
                if constexpr (player_colors::k_enabled) {
                    if (const auto* pc = registry.try_get<PlayerColor>(e); pc != nullptr) {
                        instance.tint = glm::vec4(pc->rgb, player_colors::k_blendFactor);
                    }
                }

                bonePalette.insert(bonePalette.end(), ragdollSkinMatrices.begin(), ragdollSkinMatrices.end());
                skinnedInstances.push_back(instance);
            });
        }
        renderer->setSkinnedFrame(bonePalette, skinnedInstances);
        if (collectPerf) {
            phaseStats.skinnedInstances = static_cast<std::uint32_t>(skinnedInstances.size());
            phaseStats.boneMatrices = static_cast<std::uint32_t>(bonePalette.size());
        }

        // Update hitbox capsules from bone transforms (client-side for debug visualization).
        if (charRig_.isLoaded())
            systems::updateHitboxes(registry, clientHitboxRig_, kRigScale_, rigMeshMinY_);

        // ── PR-24 fire detection: capture client view of the shot the
        // user just fired, paired against the server's `SHOT_DEBUG_REPORT`
        // by `(shooterClientId, shotInputTick)`.
        //
        // Placed RIGHT AFTER `updateHitboxes` so the captured state matches
        // exactly what the user sees on screen this frame:
        //   * `Position.value` for the local player is the post-prediction
        //     post-reconciliation value the renderer uses (LocalPlayer is
        //     excluded from `applyInterpolatedTransforms`).
        //   * `Position.value` for remote players is the PR-19 interp-
        //     delayed value (`applyInterpolatedTransforms` already ran).
        //   * `HitboxInstance.capsules` for every entity reflect the
        //     just-recomputed bones at THIS frame's pose.
        // And `snap.tick` carries the post-physics-loop value — the SAME
        // value `runInputSend` put on the wire and the server stamps on
        // its debug capture.  Pre-PR-24 the capture lived BEFORE the
        // physics loop, reading old `snap.tick` and previous-frame
        // capsules; both the off-by-one tick mismatch and the visible
        // blue-capsule-vs-actual-aim-target staleness are gone.
        bool firePulledThisFrame = false;
        registry.view<LocalPlayer, InputSnapshot>().each([&](const InputSnapshot& snap) {
            const bool wasShooting = prevShootingForDebug_;
            const bool nowShooting = snap.shooting;
            prevShootingForDebug_ = nowShooting;
            if (nowShooting && !wasShooting)
                firePulledThisFrame = true;
        });
        if (firePulledThisFrame) {
            registry.view<LocalPlayer, InputSnapshot, Position, CollisionShape, PlayerVisState>().each(
                [&](entt::entity localEntity,
                    const InputSnapshot& snap,
                    const Position& pos,
                    const CollisionShape& shape,
                    const PlayerVisState& pvis) {
                    net::shotdebug::ShotDebugCapture cap;
                    cap.shotInputTick = snap.tick; // PR-24: post-physics value, matches wire
                    const float debugEyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
                    cap.origin = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f * debugEyeDir, 0.0f};
                    const float cp = std::cos(snap.pitch);
                    cap.direction = glm::normalize(
                        glm::vec3{std::sin(snap.yaw) * cp, -std::sin(snap.pitch), std::cos(snap.yaw) * cp});
                    cap.range = physics::k_hitscanRange;

                    // PR-20.6 (root-cause fix): local raycast against
                    // capsules + world.  `resolveHitscanHitbox`
                    // unconditionally fills `hit.point` — either the
                    // closest world geometry hit, the closest capsule
                    // hit, or `origin + direction * k_hitscanRange`
                    // when nothing was struck.  We ALWAYS forward
                    // `hit.point` into `cap.hitPoint`; the rendering
                    // path uses it directly so blue tracers always
                    // terminate on whatever the client believed was
                    // in the way (a wall, a capsule, or 5000 u of air).
                    // Match the server's "cylinder hitreg": sweep the local
                    // resolve with the equipped weapon's hitscanRadius so the
                    // blue debug tracer terminates on the same fattened capsule
                    // the server will test against.
                    float localHitscanRadius = 0.0f;
                    if (const auto* ws = registry.try_get<WeaponState>(localEntity)) {
                        localHitscanRadius = getWeaponConfig(getEquippedGun(*ws).type).hitscanRadius;
                    }
                    const physics::HitboxHit localHit = physics::resolveHitscanHitbox(
                        registry, localEntity, cap.origin, cap.direction, localHitscanRadius);
                    cap.hitPoint = localHit.point;
                    cap.hitTargetClientId = net::shotdebug::k_missClientId;
                    cap.hitRegion = 0;
                    if (localHit.entity != entt::null && registry.valid(localHit.entity)) {
                        if (const auto* tcid = registry.try_get<ClientId>(localHit.entity)) {
                            cap.hitTargetClientId = static_cast<std::uint16_t>(tcid->value);
                            cap.hitRegion = static_cast<std::uint8_t>(localHit.region);
                        }
                    }

                    // PR-26 + PR-26.1: snapshot capsules for entities
                    // whose capsule-derived AABB — DILATED by an
                    // aim-margin — intersects the shot ray.  Goal is
                    // "show the targets I was approximately aiming at",
                    // not "show the targets the ray geometrically hit".
                    // PR-26's tight filter dropped near-miss targets
                    // (ray passed just outside the head capsule and the
                    // wireframe vanished); the user wanted to keep
                    // those visible so they can SEE why a miss
                    // happened (lag-comp drift, animation pose, etc).
                    // Distant unrelated enemies still skipped — they
                    // never come close to the ray.  Server-side
                    // `captureShotDebug` uses the same margin so the
                    // overlay's blue/red sets line up.
                    constexpr float shotDebugAimMargin = 50.0f;
                    auto remoteView = registry.view<HitboxInstance, ClientId>(entt::exclude<LocalPlayer>);
                    cap.targets.reserve(remoteView.size_hint());
                    for (const auto e : remoteView) {
                        if (e == localEntity)
                            continue;
                        const auto& hb = remoteView.get<HitboxInstance>(e);
                        if (hb.capsules.empty())
                            continue;

                        glm::vec3 boundsMin{std::numeric_limits<float>::max()};
                        glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};
                        for (const auto& c : hb.capsules) {
                            const glm::vec3 capRadius{c.radius + shotDebugAimMargin};
                            boundsMin = glm::min(boundsMin, glm::min(c.pointA, c.pointB) - capRadius);
                            boundsMax = glm::max(boundsMax, glm::max(c.pointA, c.pointB) + capRadius);
                        }
                        const physics::WorldAABB bounds{.min = boundsMin, .max = boundsMax};
                        float aabbDist = cap.range;
                        glm::vec3 aabbNormal{0.0f};
                        if (!physics::raycastAABB(cap.origin, cap.direction, bounds, cap.range, aabbDist, aabbNormal))
                            continue;

                        const auto& cid = remoteView.get<ClientId>(e);
                        net::shotdebug::ShotDebugCapture::Target tgt;
                        tgt.clientId = static_cast<std::uint16_t>(cid.value);
                        tgt.capsules = hb.capsules;
                        cap.targets.push_back(std::move(tgt));
                    }
                    debugUI.pushClientShot(cap);

                    // ── PR-27a: SHOT_INTENT — send client's view of the
                    // intended target's animation state at fire time.
                    // Server pairs by `(shooterClientId, shotInputTick)`,
                    // computes anim-state delta vs its own historical
                    // snapshot, logs to `server_shots.csv`.  No-op for
                    // shots that aren't aimed at anyone close.
                    //
                    // Target selection precedence:
                    //   1. Local raycast hit → that's clearly the
                    //      "intended target" (cap.hitTargetClientId).
                    //   2. Otherwise, closest near-miss capsule-AABB
                    //      candidate (first entry in `cap.targets`,
                    //      already filtered by ray-vs-AABB).
                    //   3. No candidates → 0xFFFF (no-target sentinel);
                    //      the server logs a "no target" row.
                    std::uint16_t intentTargetCid = cap.hitTargetClientId;
                    AnimSnapshot intentAnim{};
                    if (intentTargetCid == net::shotdebug::k_missClientId && !cap.targets.empty()) {
                        intentTargetCid = cap.targets.front().clientId;
                    }
                    if (intentTargetCid != net::shotdebug::k_missClientId) {
                        // Find the entity for this clientId and read its
                        // current AnimSnapshot.  Same component the
                        // animator-update loop above just refreshed.
                        registry.view<ClientId, AnimSnapshot>().each([&](const ClientId& cid, const AnimSnapshot& a) {
                            if (cid.value == intentTargetCid)
                                intentAnim = a;
                        });
                    }
                    client->sendShotIntent(snap.tick, intentTargetCid, intentAnim);
                });
        }
    }
    phaseSnap(phaseStats.animationMs);

    // Build entity render list
    {
        /////////////////////////////////////////// Entity Render List ///////////////////////////////////////////
        // Phase 5a: snapshot-rate alpha for remote-entity position lerp.
        // Read once per frame; applied to every entity below that has a
        // PreviousPosition (i.e. has received at least one snapshot).
        const float snapshotAlpha = client->getSnapshotAlpha();
        // PR-11: render-delay timestamp for non-local entities.  See the
        // animation-pass site above for the full rationale; in short,
        // playing back at `now − N × snapshotInterval` lets the renderer
        // always have a buffered "future" sample to lerp toward, hiding
        // single-snapshot losses and smoothing jitter.  0 means
        // "no buffered playback" — fall through to the Phase-5a lerp.
        const Uint64 interpRenderNs = client->getInterpolationRenderTimeNs();

        // Phase 3f: extract view frustum for culling entity render commands +
        // third-person weapons.  Same Gribb-Hartmann decomposition we use for
        // skinned chars, but reusable across both passes here.  Bounding-
        // sphere check per entity is ~24 ops — negligible — but cuts the
        // per-frame draw count by typically 50–80% on a 100-bot scene.
        const glm::mat4 cullVP = renderer->getCamera().getViewProjection();
        struct CullPlane
        {
            glm::vec3 n;
            float d;
        };
        CullPlane cullPlanes[6];
        {
            const glm::mat4 m = glm::transpose(cullVP);
            const auto extract = [&](int idx, const glm::vec4& row) {
                const glm::vec3 n(row.x, row.y, row.z);
                const float len = glm::length(n);
                cullPlanes[idx].n = n / len;
                cullPlanes[idx].d = row.w / len;
            };
            extract(0, m[3] + m[0]);
            extract(1, m[3] - m[0]);
            extract(2, m[3] + m[1]);
            extract(3, m[3] - m[1]);
            extract(4, m[3] + m[2]);
            extract(5, m[3] - m[2]);
        }
        const auto entityVisible = [&](const glm::vec3& center, float radius) {
            for (const auto& p : cullPlanes)
                if (glm::dot(p.n, center) + p.d < -radius)
                    return false;
            return true;
        };

        std::vector<EntityRenderCmd> entityCmds;

        // Inject the weapon entity render commands populated during the animation
        // pass (the right-hand-bone-parented weapons). Done before the Renderable
        // view so weapons live early in the entity command list — keeps render-order
        // deterministic across frames.
        entityCmds.reserve(entityCmds.size() + candidateWeaponCmds.size());
        entityCmds.insert(entityCmds.end(), candidateWeaponCmds.begin(), candidateWeaponCmds.end());
        candidateWeaponCmds.clear();

        registry.view<Position, Renderable>().each([&](entt::entity e, const Position& pos, const Renderable& rend) {
            if (!rend.visible || rend.modelIndex < 0)
                return;
            // Skip local player model in first-person (Option A from the plan).
            // The animator still runs so future gun-IK has an up-to-date pose;
            // only rendering is suppressed — and optionally re-enabled via the
            // "Show local body" debug toggle for third-person inspection.
            if (!animUI_.showLocalBody && registry.all_of<LocalPlayer>(e))
                return;

            // PR-19: pos.value is already pre-interpolated by
            // `Client::applyInterpolatedTransforms` for non-local
            // entities when buffered render-delay is active.  When
            // disabled, fall back to the Phase-5a (prev, cur, alpha)
            // lerp for snapshot-rate smoothness.  Same dual-path
            // pattern as the skinned-body site above.
            glm::vec3 renderPos = pos.value;
            if (interpRenderNs != 0) {
                // pos.value already pre-interpolated; no work here.
            } else if (const auto* prev = registry.try_get<PreviousPosition>(e)) {
                renderPos = glm::mix(prev->value, pos.value, snapshotAlpha);
            }

            // Frustum cull — generous radius covers any rend.scale up to ~5×
            // the rig height.  Glow spheres / map props pass this trivially
            // when in view.  Out-of-view entities skip the per-mesh draw loop.
            const float entityRadius = 80.0f * std::max({rend.scale.x, rend.scale.y, rend.scale.z, 1.0f});
            if (!entityVisible(renderPos, entityRadius))
                return;

            glm::mat4 world = glm::translate(glm::mat4(1.0f), renderPos + rend.translation);
            world *= glm::mat4_cast(rend.orientation);
            world = glm::scale(world, rend.scale);

            // Projectile entities carry a per-grenade tint (HE = green,
            // Molotov = orange, Sticky = blue). Non-projectile entities use
            // the default white tint (no recolor).
            glm::vec4 tint{1.0f};
            if (const auto* proj = registry.try_get<Projectile>(e)) {
                tint = glm::vec4(proj->tint, 1.0f);
            }

            entityCmds.push_back(EntityRenderCmd{.modelIndex = rend.modelIndex, .worldTransform = world, .tint = tint});
        });

        // (Legacy third-person remote-weapon emission removed. Every player
        // gets an AnimatedCharacter at spawn, so weapons always come from the
        // candidate-based bone-parented pipeline above.)

        // // Glow sphere — always rendered at a fixed world position for bloom testing.
        // constexpr glm::vec3 glowSpherePos{0.0f, 80.0f, 300.0f};
        // if (glowSphereModelIdx_ >= 0) {
        //     entityCmds.push_back(EntityRenderCmd{
        //         .modelIndex = glowSphereModelIdx_,
        //         .worldTransform = glm::translate(glm::mat4(1.0f), glowSpherePos),
        //     });
        // }
        //
        // // Movable glow sphere — follows the player's view direction.
        // const glm::vec3 movableSpherePos = cachedEye_ + cachedCamFwd_ * sphereFollowDist_;
        // if (movableSphereEnabled_ && movableSphereModelIdx_ >= 0) {
        //     entityCmds.push_back(EntityRenderCmd{
        //         .modelIndex = movableSphereModelIdx_,
        //         .worldTransform = glm::translate(glm::mat4(1.0f), movableSpherePos),
        //     });
        // }

        // // Glow beam cylinder — follows player position and view direction.
        // // Offsets are (forward, up, right) relative to camera.
        // const glm::vec3 camRight = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
        // const glm::vec3 camUp = glm::normalize(glm::cross(camRight, cachedCamFwd_));
        // const glm::vec3 beamWorldStart =
        //     cachedEye_ + cachedCamFwd_ * beamStartOff_.x + camUp * beamStartOff_.y + camRight * beamStartOff_.z;
        // const glm::vec3 beamWorldEnd =
        //     cachedEye_ + cachedCamFwd_ * beamEndOff_.x + camUp * beamEndOff_.y + camRight * beamEndOff_.z;
        //
        // if (beamEnabled_ && glowCylinderModelIdx_ >= 0) {
        //     // Update visual emissive color to match the color picker (HDR scaled).
        //     const float emScale = 10.0f;
        //     renderer->setModelEmissive(glowCylinderModelIdx_, glm::vec4(beamColor_ * emScale, 0.0f));
        //     entityCmds.push_back(EntityRenderCmd{
        //         .modelIndex = glowCylinderModelIdx_,
        //         .worldTransform = cylinderTransform(beamWorldStart, beamWorldEnd, beamRadius_),
        //     });
        // }

        // Weapon beam visuals — driven by BeamState synced from server registry.
        // Local player: client-side predicted raycast for zero-lag response.
        // Remote players: use the server-computed positions from BeamState.
        // registry.view<BeamState>().each([&](entt::entity e, const BeamState& beam) {
        //     if (!beam.active || glowCylinderModelIdx_ < 0)
        //         return;
        //
        //     glm::vec3 beamOrigin = beam.origin;
        //     glm::vec3 beamEnd = beam.hitPoint;
        //
        //     if (registry.all_of<LocalPlayer>(e)) {
        //         // Client-side prediction: raycast with this frame's camera
        //         // direction so the beam tracks the crosshair with zero latency.
        //         const float cosPitch = std::cos(renderPitch);
        //         const glm::vec3 fwd{
        //             std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
        //         const glm::vec3 rgt = glm::normalize(glm::cross(fwd, glm::vec3{0, 1, 0}));
        //         const glm::vec3 up = glm::normalize(glm::cross(rgt, fwd));
        //
        //         // Muzzle position from viewmodel offset.
        //         beamOrigin = renderEye + fwd * vmForward + rgt * vmRight - up * vmDown;
        //
        //         // Predicted endpoint: raycast from eye along current view.
        //         const auto predictedHit = physics::raycastWorld(renderEye, fwd, physics::activeWorld());
        //         beamEnd = predictedHit.hit ? predictedHit.point : (renderEye + fwd * 5000.0f);
        //     }
        //
        //     // Green Zarya-style tint, HDR-scaled for bloom.
        //     renderer->setModelEmissive(glowCylinderModelIdx_, glm::vec4(glm::vec3(0.3f, 1.0f, 0.2f) * 10.0f, 0.0f));
        //
        //     entityCmds.push_back(EntityRenderCmd{
        //         .modelIndex = glowCylinderModelIdx_,
        //         .worldTransform = cylinderTransform(beamOrigin, beamEnd, 2.0f),
        //     });
        // });

        // Spent-casing physics + render (casings are spawned on fire in the viewmodel step below).
        if (shellEjectModelIdx_ >= 0 && !casings_.empty()) {
            const glm::vec3 gravity{0.0f, -650.0f, 0.0f};
            for (auto& cs : casings_) {
                cs.vel += gravity * frameTime;
                cs.pos += cs.vel * frameTime;
                cs.angle += cs.spin * frameTime;
                cs.age += frameTime;
            }
            casings_.erase(
                std::remove_if(casings_.begin(), casings_.end(), [](const Casing& c) { return c.age > 1.3f; }),
                casings_.end());
            for (const auto& cs : casings_) {
                const glm::mat4 m = glm::translate(glm::mat4(1.0f), cs.pos) * glm::mat4(cs.orient) *
                                    glm::rotate(glm::mat4(1.0f), cs.angle, glm::vec3(0.0f, 0.0f, 1.0f));
                entityCmds.push_back(EntityRenderCmd{.modelIndex = shellEjectModelIdx_, .worldTransform = m});
            }
        }

        if (collectPerf)
            phaseStats.entityRenderCmds = static_cast<std::uint32_t>(entityCmds.size());
        renderer->setEntityRenderList(std::move(entityCmds));
        /////////////////////////////////////////// Entity Render List ///////////////////////////////////////////

        ////////////////////////////////////// Point Lights ///////////////////////////////////////////
        // Build dynamic point lights list.
        std::vector<PointLight> dynLights;
        std::uint32_t beamPointLights = 0;
        // // Static glow sphere point light.
        // dynLights.push_back(PointLight{
        //     .position = glowSpherePos,
        //     .color = glm::vec3(1.0f, 0.6f, 0.2f),
        //     .intensity = 5.0f,
        //     .range = 500.0f,
        // });
        //
        // // Flashlight — point light near the camera.
        // if (flashlightEnabled_) {
        //     dynLights.push_back(PointLight{
        //         .position = cachedEye_ + cachedCamFwd_ * flashlightOffset_,
        //         .color = glm::vec3(1.0f, 0.95f, 0.9f),
        //         .intensity = flashlightIntensity_,
        //         .range = flashlightRange_,
        //     });
        // }
        //
        // // Movable glow sphere point light.
        // if (movableSphereEnabled_) {
        //     dynLights.push_back(PointLight{
        //         .position = movableSpherePos,
        //         .color = glm::vec3(0.4f, 0.7f, 1.0f),
        //         .intensity = sphereIntensity_,
        //         .range = sphereRange_,
        //     });
        // }

        // // Beam point lights — evenly distributed along the beam length.
        // if (beamEnabled_) {
        //     const glm::vec3 beamDelta = beamWorldEnd - beamWorldStart;
        //     const float beamLen = glm::length(beamDelta);
        //     const int numBeamLights = (beamLightSpacing_ > 1.0f && beamLen > 0.1f)
        //                                   ? std::max(2, static_cast<int>(beamLen / beamLightSpacing_) + 1)
        //                                   : 2;
        //     const glm::vec3 beamLightColor = beamColor_ * 1.5f;
        //     for (int i = 0; i < numBeamLights; ++i) {
        //         const float t = static_cast<float>(i) / static_cast<float>(numBeamLights - 1);
        //         dynLights.push_back(PointLight{
        //             .position = beamWorldStart + beamDelta * t,
        //             .color = beamLightColor,
        //             .intensity = beamLightIntensity_,
        //             .range = beamLightRange_,
        //         });
        //     }
        // }

        // Weapon beam point lights — from BeamState, evenly distributed.
        // Local player uses predicted positions (same as the visual beam above).
        registry.view<BeamState>().each([&](entt::entity e, const BeamState& beam) {
            if (!beam.active)
                return;

            glm::vec3 lightStart = beam.origin;
            glm::vec3 lightEnd = beam.hitPoint;

            // The auto-lock Tesla beam endpoint is server-authoritative (the
            // locked target, or a forward point capped at maxRange), so the
            // lights follow it directly. Other beams (none today) keep the
            // zero-lag local raycast prediction.
            if (registry.all_of<LocalPlayer>(e) && beam.type != WeaponType::EnergyGun) {
                const float cosPitch = std::cos(renderPitch);
                const glm::vec3 fwd{
                    std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
                lightStart = renderEye;
                const auto predictedHit = physics::raycastWorld(renderEye, fwd, physics::activeWorld());
                lightEnd = predictedHit.hit ? predictedHit.point : (renderEye + fwd * 5000.0f);
            }

            const glm::vec3 delta = lightEnd - lightStart;
            const float len = glm::length(delta);
            if (len < 1.0f)
                return;
            const int numLights = std::max(2, static_cast<int>(len / 80.0f) + 1);
            const glm::vec3 lightColor{0.3f, 1.0f, 0.2f};
            for (int i = 0; i < numLights && dynLights.size() < 14; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(numLights - 1);
                dynLights.push_back(PointLight{
                    .position = lightStart + delta * t,
                    .intensity = 3.0f,
                    .color = lightColor,
                    .range = 200.0f,
                });
                ++beamPointLights;
            }
        });

        // Muzzle-flash point lights — age out transient flashes and emit the
        // survivors with an attack-decay brightness envelope so the flash
        // ignites and dissolves smoothly instead of snapping on/off:
        //   • Attack  (first k_flashAttack seconds): a fast ease-out rise from 0
        //     to full brightness — softens the hard "spawn at full" step that
        //     made rapid fire look choppy between shots.
        //   • Decay   (the rest of the lifetime): a normalized exponential
        //     exp(-k·t) that reaches exactly 0 at the end (the "-floor" term
        //     removes the step raw exp would leave). A gentle k gives a slow,
        //     gradual die-out, and the long lifetime lets consecutive shots'
        //     flashes overlap for a continuous glow.
        constexpr std::size_t k_maxDynLights = 32;
        constexpr float k_flashAttack = 0.02f;            // rise-to-full time (seconds)
        constexpr float k_flashDecay = 3.0f;              // decay steepness — smaller = more gradual
        const float flashFloor = std::exp(-k_flashDecay); // exp value at decay end, subtracted so fade hits 0
        for (auto it = transientVfxLights_.begin(); it != transientVfxLights_.end();) {
            it->age += frameTime;
            if (it->age >= it->lifetime) {
                it = transientVfxLights_.erase(it);
                continue;
            }
            if (dynLights.size() < k_maxDynLights) {
                float env;
                if (it->age < k_flashAttack) {
                    const float x = it->age / k_flashAttack; // [0, 1)
                    env = 1.0f - (1.0f - x) * (1.0f - x);    // ease-out: fast rise, smooth into peak
                } else {
                    const float decayTime = std::max(1e-4f, it->lifetime - k_flashAttack);
                    const float t = (it->age - k_flashAttack) / decayTime; // [0, 1)
                    env = (std::exp(-k_flashDecay * t) - flashFloor) / (1.0f - flashFloor);
                }
                dynLights.push_back(PointLight{
                    .position = it->position,
                    .intensity = it->intensity * env,
                    .color = it->color,
                    .range = it->range,
                });
            }
            ++it;
        }

        if (collectPerf) {
            phaseStats.pointLights = static_cast<std::uint32_t>(dynLights.size());
            phaseStats.beamPointLights = beamPointLights;
        }
        renderer->setPointLights(std::move(dynLights));
        /////////////////////////////////////////// Point Lights ///////////////////////////////////////////
    }
    phaseSnap(phaseStats.entityCmdsMs);

    // Determine equipped weapon type from WeaponState
    registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
        const GunInstance& gun = getEquippedGun(ws);
        currentEquippedType_ = gun.type;
    });

    bool hideRailgunViewmodelForScope = false;
    registry.view<LocalPlayer, InputSnapshot>().each([&](const InputSnapshot& input) {
        hideRailgunViewmodelForScope = currentEquippedType_ == WeaponType::RailGun && input.scoped;
    });
    // Hide the first-person viewmodel once the emote camera has swung to third
    // person — the third-person body (with its own attached weapon) takes over.
    const bool hideViewmodelForEmote = emoteCamBlend_ > 0.5f;

    const bool currentWeaponRenderable = isRenderableGunType(currentEquippedType_);

    // Auto-apply per-weapon viewmodel defaults when weapon changes
    if (currentWeaponRenderable && (currentEquippedType_ != lastEquippedType_ || !viewmodelDefaultsApplied_)) {
        const auto& vp = getViewmodelParams(currentEquippedType_);
        vmScale = vp.scale;
        vmForward = vp.forward;
        vmRight = vp.right;
        vmDown = vp.down;
        vmYawOffset = vp.yawOffset;
        vmPitchOffset = vp.pitchOffset;
        vmRollOffset = vp.rollOffset;
        lastEquippedType_ = currentEquippedType_;
        viewmodelDefaultsApplied_ = true;
    }

    // Per-weapon animated viewmodel: install the active weapon's gun + arms rig
    // (and its textures) into the renderer's single viewmodel slot when the
    // equipped weapon changes. Weapons without a loaded viewmodel leave the slot
    // alone and render via the static fallback path below.
    if (currentWeaponRenderable) {
        const std::size_t t = static_cast<std::size_t>(currentEquippedType_);
        if (weaponVmLoaded_[t] && static_cast<int>(t) != activeViewmodelType_) {
            renderer->setViewmodelRig(weaponVms_[t].buildRigSources(), weaponVms_[t].numJoints());
            renderer->setViewmodelTexture(weaponVmModelIdx_[t]);
            if (weaponVmArmsLoaded_[t]) {
                renderer->setViewmodelArmsRig(weaponVmArms_[t].buildRigSources(), weaponVmArms_[t].numJoints());
                if (weaponVmArmsModelIdx_[t] >= 0)
                    renderer->setViewmodelArmsTexture(weaponVmArmsModelIdx_[t]);
            }
            activeViewmodelType_ = static_cast<int>(t);
            weaponVmEquipped_ = false; // replay the draw clip for the newly-equipped weapon
            weaponVmReloadActive_ = false;
            weaponVmPrevMagAmmo_ = -1;
        }
    }

    const int currentWeaponModelIdx =
        currentWeaponRenderable ? weaponModelIndices_[static_cast<std::size_t>(currentEquippedType_)] : -1;

    // Build weapon viewmodel
    {
        WeaponViewmodel vm;
        const auto localDeadView = registry.view<LocalPlayer, RespawnTimer>();
        if (currentWeaponModelIdx >= 0 && localDeadView.begin() == localDeadView.end() &&
            !hideRailgunViewmodelForScope && !hideViewmodelForEmote)
        {
            vm.modelIndex = currentWeaponModelIdx;
            vm.visible = true;

            const float cosPitch = std::cos(renderPitch);
            const glm::vec3 forward{
                std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
            // Guard the degenerate forward∥up case so normalize can't divide by
            // ~0 and feed the viewmodel transform a NaN basis.
            const glm::vec3 rightRaw = glm::cross(forward, glm::vec3{0, 1, 0});
            const float rightLen = glm::length(rightRaw);
            glm::vec3 right = (rightLen > 1e-4f) ? (rightRaw / rightLen) : glm::vec3{1.0f, 0.0f, 0.0f};
            glm::vec3 up = glm::normalize(glm::cross(right, forward));

            // Apply camera roll to the viewmodel basis so the weapon follows
            // the 180° flip (or any tilt) instead of staying upright.
            if (std::abs(currentCameraRoll_) > 0.001f) {
                const float cosR = std::cos(currentCameraRoll_);
                const float sinR = std::sin(currentCameraRoll_);
                const glm::vec3 rolledRight = right * cosR + up * sinR;
                const glm::vec3 rolledUp = up * cosR - right * sinR;
                right = rolledRight;
                up = rolledUp;
            }

            // --- Weapon sway (CoD-style barrel lead) ---
            {
                if (!swayInitialized_) {
                    prevSwayYaw_ = renderYaw;
                    prevSwayPitch_ = renderPitch;
                    swayInitialized_ = true;
                }

                float yawDelta = renderYaw - prevSwayYaw_;
                float pitchDelta = renderPitch - prevSwayPitch_;
                prevSwayYaw_ = renderYaw;
                prevSwayPitch_ = renderPitch;

                // Wrap yaw delta for -pi/+pi boundary
                if (yawDelta > glm::pi<float>())
                    yawDelta -= glm::two_pi<float>();
                if (yawDelta < -glm::pi<float>())
                    yawDelta += glm::two_pi<float>();

                if (frameTime > 0.0001f) {
                    float targetX = std::clamp(yawDelta / frameTime * 0.05f * swayAmplitudeYaw_,
                                               -swayAmplitudeYaw_ * 3.0f,
                                               swayAmplitudeYaw_ * 3.0f);
                    float targetY = std::clamp(pitchDelta / frameTime * 0.05f * swayAmplitudePitch_,
                                               -swayAmplitudePitch_ * 3.0f,
                                               swayAmplitudePitch_ * 3.0f);

                    float alpha = std::min(1.0f, swaySmoothing_ * frameTime * 60.0f);
                    swayOffsetX_ = glm::mix(swayOffsetX_, targetX, alpha);
                    swayOffsetY_ = glm::mix(swayOffsetY_, targetY, alpha);
                }
                float decay = std::exp(-swayDecayRate_ * frameTime);
                swayOffsetX_ *= decay;
                swayOffsetY_ *= decay;
            }

            // --- Recoil decay ---
            {
                const RecoilParams& rp = getRecoilParams(currentEquippedType_);
                float decay = std::exp(-rp.recoverySpeed * frameTime);
                recoilPitch_ *= decay;
                recoilPushBack_ *= decay;
                recoilRoll_ *= decay;

                // Kill tiny residuals
                if (std::abs(recoilPitch_) < 0.01f)
                    recoilPitch_ = 0.0f;
                if (std::abs(recoilPushBack_) < 0.01f)
                    recoilPushBack_ = 0.0f;
                if (std::abs(recoilRoll_) < 0.01f)
                    recoilRoll_ = 0.0f;
            }

            // --- Apex-style camera recoil spring ---
            // Critically-damped second-order spring chases `cameraRecoilTarget*`,
            // and the per-frame delta is COMMITTED to the local player's
            // InputSnapshot.pitch/yaw — so raycasts, tracers, bullet-holes, and
            // the replicated aim all actually drift with the recoil.
            //
            // Recovery model is selectable via `useRecoilCompensation_`:
            //   A) false → target never decays. Aim stays where recoil walked it
            //      (CS-style; player must pull down to correct).
            //   B) true  → during active fire, the player's counter-mouse is
            //      subtracted from the target (debt-tracked compensation); after
            //      `recoilIdleTime_` exceeds the idle threshold, target decays
            //      toward 0 and the spring drags the aim back through whatever
            //      debt remained un-paid. This is the Apex-like model.
            {
                const float dt = frameTime;
                const float omega = cameraRecoilOmega_;
                const float k = omega * omega;
                const float c = 2.0f * omega;

                // 1) Read current snap and recover the player's mouse delta since
                //    our last commit. Anything that moved snap.pitch/yaw between
                //    end-of-last-frame and now that we did NOT do ourselves is by
                //    definition the player's mouse input.
                float currentSnapPitch = 0.0f, currentSnapYaw = 0.0f;
                bool foundSnap = false;
                registry.view<LocalPlayer, InputSnapshot>().each([&](const InputSnapshot& snap) {
                    currentSnapPitch = snap.pitch;
                    currentSnapYaw = snap.yaw;
                    foundSnap = true;
                });

                float mouseDeltaPitch = 0.0f, mouseDeltaYaw = 0.0f;
                if (foundSnap && haveLastSnap_) {
                    mouseDeltaPitch = currentSnapPitch - lastSnapPitchAfterCommit_;
                    mouseDeltaYaw = currentSnapYaw - lastSnapYawAfterCommit_;
                }

                // 2) Approach B — credit-accumulator compensation. Runs on EVERY
                //    frame (firing, recovery, idle) so post-release pull-down is
                //    still credited. Each frame's mouse delta is banked into a
                //    signed credit (clamped + slowly decaying), then consumed
                //    against target. This fixes two bugs of the naive
                //    "cap-at-current-target" comp:
                //      • Excess player input is no longer wasted — it pre-pays
                //        the next shot's kick instead of being thrown away.
                //      • Compensation still tracks while the recovery is running,
                //        so the refund doesn't add to the player's continued
                //        pull-down (the "overdoes it" bug).
                if (useRecoilCompensation_) {
                    const float creditDecay = std::exp(-compensationCreditDecay_ * dt);
                    compensationCreditPitch_ *= creditDecay;
                    compensationCreditYaw_ *= creditDecay;

                    compensationCreditPitch_ = std::clamp(
                        compensationCreditPitch_ + mouseDeltaPitch, -compensationCreditCap_, compensationCreditCap_);
                    compensationCreditYaw_ = std::clamp(
                        compensationCreditYaw_ + mouseDeltaYaw, -compensationCreditCap_, compensationCreditCap_);

                    auto consume = [](float& target, float& credit) {
                        if (target == 0.0f)
                            return;
                        if (target < 0.0f && credit > 0.0f) {
                            const float pay = std::min(credit, -target);
                            target += pay;
                            credit -= pay;
                        } else if (target > 0.0f && credit < 0.0f) {
                            const float pay = std::min(-credit, target);
                            target -= pay;
                            credit += pay;
                        }
                    };
                    consume(cameraRecoilTargetPitch_, compensationCreditPitch_);
                    consume(cameraRecoilTargetYaw_, compensationCreditYaw_);
                }

                // 3) Approach B only — idle target decay (the recovery itself).
                //    Approach A never decays; spring just asymptotes to the
                //    final accumulated debt and the aim stays kicked.
                if (useRecoilCompensation_ && recoilIdleTime_ > cameraRecoilIdleThreshold_) {
                    const float targetDecay = std::exp(-cameraRecoilTargetDecay_ * dt);
                    cameraRecoilTargetPitch_ *= targetDecay;
                    cameraRecoilTargetYaw_ *= targetDecay;
                }

                // 4) Spring step (always — drives `current → target`).
                //
                // Self-heal first: a single NaN is absorbing, so if any spring
                // state ever went non-finite (e.g. a pre-fix corrupted value, or
                // some unforeseen overflow) it would wedge the aim forever. Reset
                // to rest instead of propagating the poison.
                if (!std::isfinite(cameraRecoilPitch_) || !std::isfinite(cameraRecoilPitchVel_) ||
                    !std::isfinite(cameraRecoilYaw_) || !std::isfinite(cameraRecoilYawVel_) ||
                    !std::isfinite(cameraRecoilTargetPitch_) || !std::isfinite(cameraRecoilTargetYaw_))
                {
                    cameraRecoilPitch_ = cameraRecoilPitchVel_ = 0.0f;
                    cameraRecoilYaw_ = cameraRecoilYawVel_ = 0.0f;
                    cameraRecoilTargetPitch_ = cameraRecoilTargetYaw_ = 0.0f;
                    committedRecoilPitch_ = committedRecoilYaw_ = 0.0f;
                }

                // Semi-implicit Euler is only stable while the damping step obeys
                // c*dt < 2 (here c = 2*omega, so dt < 1/omega ≈ 0.029s at the
                // default omega). `frameTime` is capped at 0.25s upstream, so a
                // single frame hitch / alt-tab / sub-30-FPS stall feeds the raw dt
                // straight past the stability limit, the integrator diverges to
                // ±inf within a few frames, and the next step computes inf - inf =
                // NaN — which then gets committed into snap.pitch/yaw and never
                // recovers. Substep so every integration step stays well inside
                // the stability region (c*subDt = 1, k*subDt² = 0.25) no matter
                // how big the frame time is.
                const float maxSubDt = (omega > 1e-3f) ? (0.5f / omega) : dt;
                const int subSteps = (maxSubDt > 0.0f) ? std::max(1, static_cast<int>(std::ceil(dt / maxSubDt))) : 1;
                const float subDt = dt / static_cast<float>(subSteps);
                auto stepSpring = [&](float& x, float& v, float target) {
                    for (int i = 0; i < subSteps; ++i) {
                        const float a = -k * (x - target) - c * v;
                        v += a * subDt;
                        x += v * subDt;
                    }
                };
                stepSpring(cameraRecoilPitch_, cameraRecoilPitchVel_, cameraRecoilTargetPitch_);
                stepSpring(cameraRecoilYaw_, cameraRecoilYawVel_, cameraRecoilTargetYaw_);

                // 5) Commit the per-frame change in spring offset to the local
                //    player's actual aim, and save the post-commit snap value
                //    so next frame's mouse-delta math is correct.
                if (useSpringCameraRecoil_) {
                    const float dPitch = cameraRecoilPitch_ - committedRecoilPitch_;
                    const float dYaw = cameraRecoilYaw_ - committedRecoilYaw_;
                    registry.view<LocalPlayer, InputSnapshot>().each([&](InputSnapshot& snap) {
                        // Always route through applyRecoilAimDelta (even for a zero
                        // delta) so a snap that is somehow already non-finite gets
                        // sanitized rather than persisting.
                        applyRecoilAimDelta(snap, dPitch, dYaw);
                        lastSnapPitchAfterCommit_ = snap.pitch;
                        lastSnapYawAfterCommit_ = snap.yaw;
                        haveLastSnap_ = true;
                    });
                    committedRecoilPitch_ = cameraRecoilPitch_;
                    committedRecoilYaw_ = cameraRecoilYaw_;
                }

                // Kill tiny residuals so the spring fully settles.
                if (std::abs(cameraRecoilPitch_) < 1e-5f && std::abs(cameraRecoilPitchVel_) < 1e-4f) {
                    cameraRecoilPitch_ = 0.0f;
                    cameraRecoilPitchVel_ = 0.0f;
                }
                if (std::abs(cameraRecoilYaw_) < 1e-5f && std::abs(cameraRecoilYawVel_) < 1e-4f) {
                    cameraRecoilYaw_ = 0.0f;
                    cameraRecoilYawVel_ = 0.0f;
                }
                if (std::abs(cameraRecoilTargetPitch_) < 1e-5f)
                    cameraRecoilTargetPitch_ = 0.0f;
                if (std::abs(cameraRecoilTargetYaw_) < 1e-5f)
                    cameraRecoilTargetYaw_ = 0.0f;
            }

            // --- Reload animation ---
            {
                auto smooth = [](float e0, float e1, float x) {
                    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
                    return t * t * (3.0f - 2.0f * t);
                };

                float reloadOffset = 0.0f;
                registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
                    const GunInstance& gun = getEquippedGun(ws);
                    const WeaponConfig& cfg = getWeaponConfig(gun.type);
                    if (gun.isReloading && cfg.reloadTime > 0.0f) {
                        float t = 1.0f - (gun.reloadTime / cfg.reloadTime);
                        constexpr float kReloadMaxDrop = 80.0f;
                        constexpr float kReloadMovePercent = 0.15f;
                        if (t < kReloadMovePercent) {
                            reloadOffset = kReloadMaxDrop * smooth(0.0f, kReloadMovePercent, t);
                        } else if (t < 1.0f - kReloadMovePercent) {
                            reloadOffset = kReloadMaxDrop;
                        } else {
                            reloadOffset = kReloadMaxDrop * (1.0f - smooth(1.0f - kReloadMovePercent, 1.0f, t));
                        }
                    }
                });

                // Animated-viewmodel weapons play a real reload clip, so suppress
                // the legacy "weapon-down" drop for any weapon whose viewmodel loaded.
                if (currentWeaponRenderable && weaponVmLoaded_[static_cast<std::size_t>(currentEquippedType_)])
                    reloadOffset = 0.0f;
                reloadDownwardOffset_ = reloadOffset;
            }

            // --- Grenade throw animation ---
            // Same dip as the reload, but on a fixed 0.3s window. The throw cooldown is
            // longer than the animation, so drive progress off elapsed time since the
            // throw rather than the remaining cooldown.
            {
                auto smooth = [](float e0, float e1, float x) {
                    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
                    return t * t * (3.0f - 2.0f * t);
                };

                float throwOffset = 0.0f;
                registry.view<LocalPlayer, GrenadeState>().each([&](const GrenadeState& grenades) {
                    const float throwCooldown = getGrenadeConfig(grenades.selected).throwCooldown;
                    const float elapsed = throwCooldown - grenades.cooldown;
                    if (grenades.cooldown > 0.0f && elapsed >= 0.0f && elapsed < kGrenadeThrowAnimTime) {
                        float t = elapsed / kGrenadeThrowAnimTime;
                        constexpr float kThrowMaxDrop = 80.0f;
                        constexpr float kThrowMovePercent = 0.15f;
                        if (t < kThrowMovePercent) {
                            throwOffset = kThrowMaxDrop * smooth(0.0f, kThrowMovePercent, t);
                        } else if (t < 1.0f - kThrowMovePercent) {
                            throwOffset = kThrowMaxDrop;
                        } else {
                            throwOffset = kThrowMaxDrop * (1.0f - smooth(1.0f - kThrowMovePercent, 1.0f, t));
                        }
                    }
                });

                grenadeThrowDownwardOffset_ = throwOffset;
            }

            // --- Velocity-direction-dependent bobbing ---
            float bobPhase = 0.0f;
            float bobAmpFwd = 0.0f;
            float bobAmpStrafe = 0.0f;

            registry.view<LocalPlayer, Velocity>().each([&](const Velocity& vel) {
                glm::vec3 hVel{vel.value.x, 0.0f, vel.value.z};
                glm::vec3 hFwd{forward.x, 0.0f, forward.z};
                float hFwdLen = glm::length(hFwd);
                if (hFwdLen < 0.001f) {
                    hFwd = glm::vec3{std::sin(renderYaw), 0.0f, std::cos(renderYaw)};
                } else {
                    hFwd /= hFwdLen;
                }
                glm::vec3 hRight = glm::normalize(glm::cross(hFwd, glm::vec3{0, 1, 0}));

                float fwdSpeed = std::abs(glm::dot(hVel, hFwd));
                float strafeSpeed = std::abs(glm::dot(hVel, hRight));

                bobPhase = static_cast<float>(SDL_GetTicks()) * 0.008f;

                if (fwdSpeed > 10.0f)
                    bobAmpFwd = std::min(fwdSpeed / 800.0f, 1.5f);
                if (strafeSpeed > 10.0f)
                    bobAmpStrafe = std::min(strafeSpeed / 800.0f, 1.0f);
            });

            // Forward: classic vertical-dominant bob
            float bobX = std::sin(bobPhase) * bobAmpFwd * 0.3f;
            float bobY = std::sin(bobPhase * 2.0f) * bobAmpFwd * 0.5f;

            // Strafe: horizontal-dominant bob at slightly different frequency
            bobX += std::sin(bobPhase * 0.9f) * bobAmpStrafe * 0.8f;
            bobY += std::sin(bobPhase * 1.8f) * bobAmpStrafe * 0.2f;

            // Position the weapon in camera space, then convert to world.
            glm::vec3 weaponPos = renderEye + forward * vmForward + right * vmRight - up * vmDown;
            // Apply sway
            weaponPos += right * swayOffsetX_ + up * swayOffsetY_;
            // Apply bob
            weaponPos += right * bobX + up * bobY;
            // Apply recoil pushback
            weaponPos -= forward * recoilPushBack_;
            // Apply reload downward offset
            weaponPos -= up * reloadDownwardOffset_;
            // Apply grenade throw downward offset
            weaponPos -= up * grenadeThrowDownwardOffset_;

            // Build world transform: translate -> local-rotate -> camera-orient -> scale.
            //
            // 1) Camera orientation: maps model axes into camera space.
            const glm::mat4 cameraOrient = glm::mat4(glm::vec4(right, 0.0f),
                                                     glm::vec4(up, 0.0f),
                                                     glm::vec4(forward, 0.0f),
                                                     glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

            // 2) Local rotation offsets (yaw/pitch/roll in degrees) with recoil.
            const glm::mat4 localRot =
                glm::rotate(glm::mat4(1.0f), glm::radians(vmYawOffset), glm::vec3(0, 1, 0)) *
                glm::rotate(glm::mat4(1.0f), glm::radians(vmPitchOffset + recoilPitch_), glm::vec3(1, 0, 0)) *
                glm::rotate(glm::mat4(1.0f), glm::radians(vmRollOffset + recoilRoll_), glm::vec3(0, 0, 1));
            const glm::mat4 weaponOrientation = cameraOrient * localRot;

            glm::mat4 weaponWorld = glm::translate(glm::mat4(1.0f), weaponPos);
            weaponWorld *= weaponOrientation;
            // Negate X to cancel the reflection in the camera orient matrix
            // (right, up, forward has det = -1).
            weaponWorld = glm::scale(weaponWorld, glm::vec3(-vmScale, vmScale, vmScale));

            vm.transform = weaponWorld;
            const auto& fpHands = fpHandMountParams_[static_cast<int>(currentEquippedType_)];
            const Asset::Model* currentWeaponModel = modelFromRendererIndex(currentWeaponModelIdx);
            cachedRightPalmValid_ = false;
            if (viewmodelRightHandModelIdx_ >= 0) {
                vm.hands.right.modelIndex = viewmodelRightHandModelIdx_;
                vm.hands.right.visible = true;
                vm.hands.right.transform = makeViewmodelHandTransform(weaponPos,
                                                                      weaponWorld,
                                                                      weaponOrientation,
                                                                      currentWeaponModel,
                                                                      "ik_r_palm",
                                                                      fpHands.rightArm.palm,
                                                                      fpHands.scale);
                // Cache the right palm's world position (translation column) so
                // the muzzle flash can spawn relative to it (see muzzleFlashOrigin).
                cachedRightPalmWorld_ = glm::vec3(vm.hands.right.transform[3]);
                cachedRightPalmValid_ = true;
            }
            if (viewmodelLeftHandModelIdx_ >= 0) {
                vm.hands.left.modelIndex = viewmodelLeftHandModelIdx_;
                vm.hands.left.visible = true;
                vm.hands.left.transform = makeViewmodelHandTransform(weaponPos,
                                                                     weaponWorld,
                                                                     weaponOrientation,
                                                                     currentWeaponModel,
                                                                     "ik_l_palm",
                                                                     fpHands.leftArm.palm,
                                                                     fpHands.scale);
            }

            if (handMountDebugMarkerModelIdx_ >= 0 && handMountDebugTarget_.space == HandMountDebugSpace::FirstPerson &&
                handMountDebugTarget_.weaponIdx == static_cast<int>(currentEquippedType_))
            {
                const FirstPersonArmMountSet& arm = handMountDebugTarget_.left ? fpHands.leftArm : fpHands.rightArm;
                glm::vec3 debugOffset = arm.palm.offset;
                bool fingerTarget = false;
                std::size_t fingerIndex = 0;
                switch (handMountDebugTarget_.point) {
                case HandMountDebugPoint::Shoulder:
                    debugOffset = arm.shoulderOffset;
                    break;
                case HandMountDebugPoint::Elbow:
                    debugOffset = arm.elbowOffset;
                    break;
                case HandMountDebugPoint::Palm:
                    debugOffset = arm.palm.offset;
                    break;
                case HandMountDebugPoint::Finger0:
                case HandMountDebugPoint::Finger1:
                case HandMountDebugPoint::Finger2:
                case HandMountDebugPoint::Finger3:
                case HandMountDebugPoint::Finger4: {
                    const auto index = static_cast<std::size_t>(handMountDebugTarget_.point) -
                                       static_cast<std::size_t>(HandMountDebugPoint::Finger0);
                    if (index < arm.fingers.size()) {
                        fingerTarget = true;
                        fingerIndex = index;
                    }
                    break;
                }
                }

                glm::vec3 debugPoint = weaponPos + transformDirection(weaponOrientation, debugOffset);
                if (fingerTarget) {
                    const glm::mat4 palmOrientation = weaponOrientation * handMountRotation(arm.palm);
                    const glm::vec3 palmPoint = weaponPos + transformDirection(weaponOrientation, arm.palm.offset);
                    debugPoint = palmPoint + transformDirection(palmOrientation, arm.fingers[fingerIndex].offset);
                }
                vm.debugPoint.modelIndex = handMountDebugMarkerModelIdx_;
                vm.debugPoint.visible = true;
                vm.debugPoint.transform = glm::scale(glm::translate(glm::mat4(1.0f), debugPoint), glm::vec3(2.0f));
            }

            cachedMuzzleValid_ = false;

            if (currentWeaponModel != nullptr && currentWeaponModel->hasMuzzle) {
                cachedMuzzleWorld_ = glm::vec3(weaponWorld * glm::vec4(currentWeaponModel->muzzleLocalPos, 1.0f));
                cachedMuzzleValid_ = true;
            }
        }
        // --- Animated R-301 first-person viewmodel (skinned, replaces the
        // static gun model + the weapon-down reload with the real Apex clips) ---
        const std::size_t vmType = currentWeaponRenderable ? static_cast<std::size_t>(currentEquippedType_) : 0;
        if (currentWeaponRenderable && weaponVmLoaded_[vmType] && vm.visible) {
            WeaponViewmodelAnim& vmGun = weaponVms_[vmType];
            WeaponViewmodelAnim& vmArms = weaponVmArms_[vmType];
            const bool hasArms = weaponVmArmsLoaded_[vmType];
            bool reloading = false;
            float reloadTotal = 0.0f;
            int magAmmo = -1;
            registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
                const GunInstance& gun = getEquippedGun(ws);
                const WeaponConfig& cfg = getWeaponConfig(gun.type);
                reloading = gun.isReloading;
                reloadTotal = cfg.reloadTime;
                magAmmo = gun.currentMagAmmo;
            });
            // A shot drops mag ammo this frame (reload raises it, so no false trigger) -> kick bolt + eject casing.
            if (weaponVmPrevMagAmmo_ >= 0 && magAmmo >= 0 && magAmmo < weaponVmPrevMagAmmo_) {
                vmGun.triggerFire();
                // Casing eject requires the chamber bone; weapons without def_c_bolt
                // (boneModelPos returns ~origin) skip it gracefully.
                const glm::vec3 boltLocal = vmGun.boneModelPos("def_c_bolt");
                if (shellEjectModelIdx_ >= 0 && casings_.size() < 64 && glm::length(boltLocal) > 1e-5f) {
                    const float cp = std::cos(renderPitch);
                    const glm::vec3 fwd{std::sin(renderYaw) * cp, -std::sin(renderPitch), std::cos(renderYaw) * cp};
                    const glm::vec3 rgt = glm::normalize(glm::cross(fwd, glm::vec3{0.0f, 1.0f, 0.0f}));
                    const glm::vec3 upv = glm::normalize(glm::cross(rgt, fwd));
                    const float j = float((casingSpawnCounter_++ * 2654435761u) % 1000u) / 1000.0f - 0.5f;
                    Casing cs;
                    // Eject from the real chamber (def_c_bolt) and orient parallel to the barrel
                    // (def_c_bolt -> muzzle_flash), both taken from the rig and transformed to world.
                    const glm::vec3 chamberWorld = glm::vec3(vm.transform * glm::vec4(boltLocal, 1.0f));
                    const glm::vec3 muzzleWorld =
                        glm::vec3(vm.transform * glm::vec4(vmGun.boneModelPos("muzzle_flash"), 1.0f));
                    glm::vec3 barrelDir = muzzleWorld - chamberWorld;
                    barrelDir = (glm::length(barrelDir) > 1e-4f) ? glm::normalize(barrelDir) : fwd;
                    // Casing long axis (local X) points opposite the muzzle (it was spawning 180-degrees backwards).
                    const glm::vec3 longAxis = -barrelDir;
                    const glm::vec3 refUp =
                        (std::abs(longAxis.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                    const glm::vec3 cz = glm::normalize(glm::cross(longAxis, refUp));
                    const glm::vec3 cy = glm::cross(cz, longAxis);
                    cs.orient = glm::mat3(longAxis, cy, cz); // local X -> -barrel (correct facing)
                    cs.pos = chamberWorld + rgt * 1.5f;      // ejection port: just right of the chamber
                    cs.vel = rgt * (150.0f + j * 40.0f) + upv * (120.0f + j * 30.0f) + fwd * (j * 40.0f);
                    cs.spin = 20.0f + j * 8.0f;
                    casings_.push_back(cs);
                }
            }
            weaponVmPrevMagAmmo_ = magAmmo;

            // On equip, play the draw clip — it ENDS at the "ready" pose, which
            // is the pose-space all the other clips (reload/etc.) live in.  This
            // is the idle base; holding draw's last frame == ready.  (Do NOT use
            // the skeleton bind pose as idle — it is ~13u off from the animated
            // poses and makes reload appear to jump/vanish out of frame.)
            if (!weaponVmEquipped_) {
                vmGun.playClip("draw", /*loop=*/false, 1.0f);
                if (hasArms)
                    vmArms.playClip("draw", /*loop=*/false, 1.0f);
                weaponVmEquipped_ = true;
                weaponVmReloadActive_ = false;
            }
            if (reloading && !weaponVmReloadActive_) {
                const float clipDur = vmGun.clipDuration("reload");
                const float speed = (reloadTotal > 0.05f && clipDur > 0.05f) ? (clipDur / reloadTotal) : 1.0f;
                vmGun.playClip("reload", /*loop=*/false, speed);
                if (hasArms)
                    vmArms.playClip("reload", /*loop=*/false, speed);
                weaponVmReloadActive_ = true;
            } else if (!reloading && weaponVmReloadActive_) {
                // Reload ends back at the ready pose, so just hold its last frame.
                weaponVmReloadActive_ = false;
            }
            vmGun.update(frameTime);

            // Drive the muzzle origin (tracers/bullets/beams read cachedMuzzleWorld_)
            // from the rig's muzzle_flash bone. Weapons lacking it leave the muzzle
            // invalid so the shooter code falls back to the camera-forward origin.
            const glm::vec3 muzLocal = vmGun.boneModelPos("muzzle_flash");
            if (glm::length(muzLocal) > 1e-5f) {
                cachedMuzzleWorld_ = glm::vec3(vm.transform * glm::vec4(muzLocal, 1.0f));
                cachedMuzzleValid_ = true;
            }

            SkinnedInstance inst;
            inst.worldTransform = vm.transform;
            inst.paletteBase = 0;
            renderer->setViewmodelFrame(vmGun.skinMatrices(), {inst});

            // Hands ride the same clip + the same viewmodel transform as the gun.
            if (hasArms) {
                vmArms.update(frameTime);
                renderer->setViewmodelArmsFrame(vmArms.skinMatrices(), {inst});
            }

            // The animated skinned gun + hands replace the static gun + viewmodel hands.
            vm.visible = false;
            vm.hands.right.visible = false;
            vm.hands.left.visible = false;
        } else {
            weaponVmEquipped_ = false; // re-draw next time an animated-viewmodel weapon is equipped
            renderer->setViewmodelFrame({}, {});
            renderer->setViewmodelArmsFrame({}, {});
        }
        renderer->setWeaponViewmodel(vm);
    }
    phaseSnap(phaseStats.viewmodelMs);

    // 7. Frame recording (R key) -- anchored to physics ticks
    if (physicsRan && recorder.isRecording()) {
        FrameState state;
        state.frameNumber = frameCount;
        state.timestamp = static_cast<double>(SDL_GetTicks()) / 1000.0 - recorder.startTimeSecs();
        state.tickCount = tickCount;
        state.renderEye = renderEye;
        state.renderYaw = renderYaw;
        state.renderPitch = renderPitch;

        registry.view<LocalPlayer, Position, Velocity, InputSnapshot>().each(
            [&](const Position& pos, const Velocity& vel, const InputSnapshot& input) {
                state.physPos = pos.value;
                state.physVel = vel.value;
                state.yaw = input.yaw;
                state.pitch = input.pitch;
            });

        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        const float winWf = static_cast<float>(winW);
        const float winHf = static_cast<float>(winH);

        const float cosPitch = std::cos(renderPitch);
        const glm::vec3 fwd{std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
        const glm::mat4 view = glm::lookAt(renderEye, renderEye + fwd, glm::vec3{0, 1, 0});
        const glm::mat4 proj = glm::perspective(
            verticalFovRadiansFromHorizontal(horizontalFovDegrees, (winHf > 0.0f) ? winWf / winHf : 1.0f),
            (winHf > 0.0f) ? winWf / winHf : 1.0f,
            5.0f,
            15000.0f);
        const glm::mat4 vp = proj * view;

        const auto toScreen = [&](glm::vec3 p) -> glm::vec2 {
            const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
            if (clip.w <= 0.0f)
                return {-1.0f, -1.0f};
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return {(ndc.x * 0.5f + 0.5f) * winWf, (1.0f - (ndc.y * 0.5f + 0.5f)) * winHf};
        };
        state.cubeScreen = toScreen(glm::vec3{0.0f, 32.0f, 400.0f});
        state.modelScreen = toScreen(glm::vec3{200.0f, 0.0f, 400.0f});

        char capPath[512];
        std::snprintf(capPath,
                      sizeof(capPath),
                      "%s/frame_%06llu.png",
                      recorder.sessionDir().c_str(),
                      static_cast<unsigned long long>(frameCount));
        state.screenshotPath = capPath;

        recorder.recordFrame(state);
    }

    // 8. FPS sample -- count this rendered frame (drives the average "cur" FPS)
    // and record the inter-render delta into the ring buffer (drives min/max and
    // the 1%/5% lows). This block only runs on frames that actually render (we
    // returned early above otherwise), so the count is true rendered frames.
    ++statsRenderFrames;
    if (prevRenderTime != 0) {
        const float renderDt = static_cast<float>(k_now - prevRenderTime) / static_cast<float>(k_perfFreq);
        if (renderDt > 0.0f && renderDt < 1.0f) { // ignore startup / minimised outliers
            fpsHistory[fpsHistoryHead] = 1.0f / renderDt;
            fpsHistoryHead = (fpsHistoryHead + 1) % k_fpsHistorySize;
            if (fpsHistoryCount < k_fpsHistorySize)
                ++fpsHistoryCount;
        }
    }
    prevRenderTime = k_now;

    ++frameCount;
    phaseSnap(phaseStats.recorderFpsMs);

    // 9. VSync toggle -- apply when limitFPSToMonitor changes
    // buildUI may modify limitFPSToMonitor, so we snapshot it before and
    // call setVSync only when it actually flips (avoids per-frame API calls).
    const bool prevLimitFPS = limitFPSToMonitor;

    // 10. Render
    debugUI.newFrame();

    // Bench mode skips the heavy debug panels; ImGui still draws but the
    // per-frame Build*UI cost is the difference between a few thousand ALU
    // ops and ~hundred-microsecond ImGui buffer construction at the high
    // bench frame-rates.
    if (!benchActive_) {
        // Unified debug menu — one window with toggles for every debug panel.
        debugUI.buildDebugMenu({
            {"HUD Tweaker", &showHudDebug_},
            {"Menu Theme Tweaker", &showMenuThemeUI_},
            {"Viewmodel Tweaker", &showViewmodelUI},
            {"Weapon Hold Tweaker", &showWeaponHoldUI_},
            {"FP Arm Tweaker", &showFPHandMountUI_},
            {"Weapon Spawner Tweaker", &showWeaponSpawnerModelUI_},
            {"Dynamic Lighting", &showDynLightUI_},
            {"HDR Debug", &showHdrDebugUI_},
            {"Animation Tester", &animUI_.show},
        });
        bool physicsCsvRecording = false;
        if (debugUI.consumePhysicsCsvRecordingRequest(physicsCsvRecording) && client != nullptr)
            client->sendPhysicsDiagRecording(physicsCsvRecording);

        debugUI.buildUI(registry,
                        tickCount,
                        mouseSensitivity,
                        gamepadLookSensitivity,
                        aimAssistCfg_,
                        renderSeparateFromPhysics,
                        inputSyncedWithPhysics,
                        limitFPSToMonitor,
                        measuredPhysicsHz,
                        statsFPSCurrent,
                        statsFPSMin,
                        statsFPSMax,
                        statsFPS1pLow,
                        statsFPS5pLow);
        debugUI.buildNetworkUI(client->getNetStats());
    }

    // All of the optional debug panels below are skipped in bench mode — none
    // of them are visible to the user during a 25 s perf run, and at 1000+ fps
    // even sub-microsecond ImGui widget allocs add up.  The buildHitboxUI
    // call in particular projects every capsule for every entity into screen
    // space, which scales linearly with bot count.
    if (!benchActive_) {
        // Phase 6 testing: network simulator window (latency + packet loss).
        // Slider values flow from DebugUI → Client each frame; idempotent when
        // unchanged (Client clamps and atomically stores). Latency is split
        // half-and-half across outbound + inbound delay queues; loss is an
        // independent Bernoulli drop applied per-datagram in each direction.
        debugUI.buildNetworkSimUI();
        client->setSimulatedLatencyMs(debugUI.getSimulatedLatencyMs());
        client->setSimulatedLossPercent(debugUI.getSimulatedLossPercent());

        // Process ammo refill request — pulse refillAmmo on InputSnapshot for
        // exactly one frame so the server handles it once then stops.
        {
            const bool wantRefill = debugUI.pendingAmmoRefill_;
            debugUI.pendingAmmoRefill_ = false;
            registry.view<LocalPlayer, InputSnapshot>().each(
                [wantRefill](InputSnapshot& snap) { snap.refillAmmo = wantRefill; });
        }

        // Process weapon-slot swap requests from the Weapon HUD debug menu.
        // ALWAYS overwrite the snap field — when there's no pending change the
        // value is -1, which clears any prior frame's request. Without this,
        // the snap held the last requested value forever, the server applied
        // it every tick, and the slot's ammo never decreased ("auto-refill" bug).
        {
            const std::int8_t wantPrimary = debugUI.pendingSetPrimaryWeapon_;
            const std::int8_t wantSecondary = debugUI.pendingSetSecondaryWeapon_;
            debugUI.pendingSetPrimaryWeapon_ = -1;
            debugUI.pendingSetSecondaryWeapon_ = -1;
            registry.view<LocalPlayer, InputSnapshot>().each([&](InputSnapshot& snap) {
                snap.debugSetPrimaryWeapon = wantPrimary;
                snap.debugSetSecondaryWeapon = wantSecondary;
            });
        }
        debugUI.buildParticleUI(particleSystem, cachedEye_, cachedCamFwd_);
        buildAnimationTesterUI(animUI_, registry, kRigScale_, kRigVerticalOffset_);

        // Hitbox debug visualization — project capsules into screen space.
        {
            int winW = 0, winH = 0;
            SDL_GetWindowSize(window, &winW, &winH);
            const float winWf = static_cast<float>(winW);
            const float winHf = static_cast<float>(winH);
            const glm::mat4 hbView = glm::lookAt(cachedEye_, cachedEye_ + cachedCamFwd_, glm::vec3{0, 1, 0});
            const glm::mat4 hbProj = glm::perspective(
                verticalFovRadiansFromHorizontal(horizontalFovDegrees, (winHf > 0.0f) ? winWf / winHf : 1.0f),
                (winHf > 0.0f) ? winWf / winHf : 1.0f,
                5.0f,
                15000.0f);
            const glm::mat4 hbVP = hbProj * hbView;
            debugUI.buildHitboxUI(registry, clientHitboxRig_, hbVP, winWf, winHf);
            debugUI.buildCollisionUI(physics::activeWorld(), hbVP, winWf, winHf);
            debugUI.buildContactDebugUI(hbVP, winWf, winHf);
            debugUI.buildWeaponSpawnerUI(registry, hbVP, winWf, winHf);
            debugUI.buildDroppedWeaponUI(registry, hbVP, winWf, winHf);
            debugUI.buildSpawnPointUI(registry, hbVP, winWf, winHf);
            // PR-20: CSGO sv_showimpacts-style shot debug.  Window
            // toggles + ring-buffer slider + per-shot summary table;
            // when `drawShotDebugOverlay` is checked we also render
            // the blue (client) / red (server-rewound) capsule pairs
            // in the 3D world via the same VP we just built for the
            // hitbox overlay.
            debugUI.buildShotDebugUI(hbVP, winWf, winHf);
        }
        HudDebugPanel::build(hud_, &showHudDebug_);
        menu_theme::buildTweaker(&showMenuThemeUI_);
    }

    // Viewmodel Tweaker — live-adjust weapon position, rotation, scale.
    if (showViewmodelUI) {
        if (ImGui::Begin("Viewmodel Tweaker", &showViewmodelUI)) {
            ImGui::SeparatorText("Position (Quake units)");
            ImGui::DragFloat("Forward", &vmForward, 0.5f, -50.0f, 100.0f, "%.1f");
            ImGui::DragFloat("Right", &vmRight, 0.5f, -50.0f, 50.0f, "%.1f");
            ImGui::DragFloat("Down", &vmDown, 0.5f, -50.0f, 80.0f, "%.1f");

            ImGui::SeparatorText("Rotation (degrees)");
            ImGui::DragFloat("Yaw", &vmYawOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("Pitch", &vmPitchOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("Roll", &vmRollOffset, 1.0f, -180.0f, 180.0f, "%.1f");

            ImGui::SeparatorText("Scale");
            ImGui::DragFloat("Scale", &vmScale, 0.001f, 0.001f, 10.0f, "%.4f");

            ImGui::Separator();
            ImGui::Text("Equipped: %s", renderableWeaponDisplayName(currentEquippedType_));

            if (ImGui::Button("Load weapon defaults")) {
                const auto& vp = getViewmodelParams(currentEquippedType_);
                vmScale = vp.scale;
                vmForward = vp.forward;
                vmRight = vp.right;
                vmDown = vp.down;
                vmYawOffset = vp.yawOffset;
                vmPitchOffset = vp.pitchOffset;
                vmRollOffset = vp.rollOffset;
            }

            ImGui::SeparatorText("Sway");
            ImGui::DragFloat("Sway Yaw Amp", &swayAmplitudeYaw_, 0.1f, 0.0f, 20.0f);
            ImGui::DragFloat("Sway Pitch Amp", &swayAmplitudePitch_, 0.1f, 0.0f, 20.0f);
            ImGui::DragFloat("Sway Decay", &swayDecayRate_, 0.5f, 1.0f, 30.0f);
            ImGui::DragFloat("Sway Smooth", &swaySmoothing_, 0.01f, 0.01f, 1.0f);

            ImGui::SeparatorText("Recoil (viewmodel)");
            ImGui::Text("Pitch: %.2f  PushBack: %.2f  Roll: %.2f",
                        static_cast<double>(recoilPitch_),
                        static_cast<double>(recoilPushBack_),
                        static_cast<double>(recoilRoll_));

            ImGui::SeparatorText("Camera Recoil (Apex spring — commits to aim)");
            ImGui::DragFloat("Pattern strength ×", &recoilPatternScaleMultiplier_, 0.05f, 0.0f, 10.0f, "%.2f");
            ImGui::Checkbox("Spring path (un-check = legacy instant snap-pitch)", &useSpringCameraRecoil_);
            {
                static const char* k_modes[] = {
                    "A: No recovery (CS-style; pull-down is yours)",
                    "B: Compensated recovery (Apex-like; debt-tracked)",
                };
                int modeIdx = useRecoilCompensation_ ? 1 : 0;
                if (ImGui::Combo("Recovery mode", &modeIdx, k_modes, IM_ARRAYSIZE(k_modes)))
                    useRecoilCompensation_ = (modeIdx == 1);
            }
            ImGui::DragFloat("Spring omega (rad/s)", &cameraRecoilOmega_, 0.5f, 4.0f, 80.0f, "%.1f");
            ImGui::DragFloat("Recovery decay (1/s)", &cameraRecoilTargetDecay_, 0.25f, 0.5f, 30.0f, "%.2f");
            ImGui::DragFloat("Idle threshold (s)", &cameraRecoilIdleThreshold_, 0.01f, 0.0f, 1.5f, "%.2f");
            ImGui::DragFloat("Credit cap (rad)", &compensationCreditCap_, 0.005f, 0.0f, 0.5f, "%.3f");
            ImGui::DragFloat("Credit decay (1/s)", &compensationCreditDecay_, 0.05f, 0.0f, 10.0f, "%.2f");
            ImGui::Text("Credit: pitch=%.3f° yaw=%.3f°",
                        static_cast<double>(glm::degrees(compensationCreditPitch_)),
                        static_cast<double>(glm::degrees(compensationCreditYaw_)));
            ImGui::Text("Pitch:  cur=%.3f° tgt=%.3f° vel=%.3f",
                        static_cast<double>(glm::degrees(cameraRecoilPitch_)),
                        static_cast<double>(glm::degrees(cameraRecoilTargetPitch_)),
                        static_cast<double>(glm::degrees(cameraRecoilPitchVel_)));
            ImGui::Text("Yaw:    cur=%.3f° tgt=%.3f° vel=%.3f",
                        static_cast<double>(glm::degrees(cameraRecoilYaw_)),
                        static_cast<double>(glm::degrees(cameraRecoilTargetYaw_)),
                        static_cast<double>(glm::degrees(cameraRecoilYawVel_)));
        }
        ImGui::End();
    }

    // Kill feed — tick timers and drop expired entries.
    for (auto& e : killFeed)
        e.displayTimer -= frameTime;
    std::erase_if(killFeed, [](const KillFeedEvent& e) { return e.displayTimer <= 0.0f; });

    // Kill feed overlay — now handled by HUD KillFeed widget.

    // Death HUD — bottom bar shown while local player is dead.
    {
        entt::entity localPlayer = entt::null;
        registry.view<LocalPlayer>().each([&](entt::entity e) { localPlayer = e; });

        if (localPlayer != entt::null && registry.all_of<DeathInfo, RespawnTimer>(localPlayer)) {
            const auto& deathInfo = registry.get<DeathInfo>(localPlayer);
            const auto& respawnTimer = registry.get<RespawnTimer>(localPlayer);

            ClientId localClientId{-1};
            registry.view<LocalPlayer, ClientId>().each([&](const ClientId& cid) { localClientId = cid; });

            char killerBuf[32];
            const char* killerName;
            if (localClientId.value != -1 && deathInfo.killerId == localClientId)
                killerName = "yourself";
            else
                killerName = lookupPlayerName(registry, deathInfo.killerId, killerBuf, sizeof(killerBuf));

            char line1[64], line2[96], line3[48];
            std::snprintf(line1, sizeof(line1), "Killed by: %s", killerName);
            std::snprintf(line2,
                          sizeof(line2),
                          "Their HP: %.0f  Armor: %.0f  |  Respawning in %.0fs",
                          static_cast<double>(deathInfo.killerHealth.health),
                          static_cast<double>(deathInfo.killerHealth.armor),
                          std::ceil(static_cast<double>(respawnTimer.timeRemaining)));
            std::snprintf(line3, sizeof(line3), "Press SPACE to skip");

            int winW = 0, winH = 0;
            SDL_GetWindowSizeInPixels(window, &winW, &winH);

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImFont* font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize();

            static constexpr float k_padX = 12.0f;
            static constexpr float k_padY = 8.0f;
            static constexpr float k_marginB = 16.0f;
            static constexpr float k_lineGap = 4.0f;

            const float boxH = fs * 3.0f + k_lineGap * 2.0f + k_padY * 2.0f;
            const float boxW = static_cast<float>(winW) * 0.4f;
            const float x = (static_cast<float>(winW) - boxW) * 0.5f;
            const float y = static_cast<float>(winH) - boxH - k_marginB;

            const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f));
            const ImU32 fg = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            const ImU32 fg2 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.85f, 0.85f, 0.85f, 0.85f));
            const ImU32 fg3 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.9f, 0.6f, 0.9f));

            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH), bg, 4.0f);
            dl->AddText(font, fs, ImVec2(x + k_padX, y + k_padY), fg, line1);
            dl->AddText(font, fs, ImVec2(x + k_padX, y + k_padY + fs + k_lineGap), fg2, line2);
            dl->AddText(font, fs, ImVec2(x + k_padX, y + k_padY + fs * 2.0f + k_lineGap * 2.0f), fg3, line3);
        }
    }

    // Hitmarker — now handled by HUD HitMarkerWidget.
    // Timer still ticks for HUD integration (hitmarkerTimer_ feeds HudGameState::hitConfirms).
    if (hitmarkerTimer_ > 0.0f)
        hitmarkerTimer_ -= frameTime;

    // Weapon Hold tweaker — spine-relative gun placement + pure-FK arm/finger pose.
    // Replaces the old 3P Weapon / Hand Mount / Grip Pose tweakers: the weapon is
    // a rigid child of Spine2 and every arm/hand bone is posed by two authored
    // rotation angles (pitch, yaw). Tune on a REMOTE player (third-person view).
    if (showWeaponHoldUI_) {
        if (ImGui::Begin("Weapon Hold Tweaker", &showWeaponHoldUI_)) {
            ImGui::Combo("Weapon",
                         &tpTuneWeaponIdx_,
                         kRenderableWeaponDisplayNames.data(),
                         static_cast<int>(kRenderableWeaponDisplayNames.size()));
            const std::size_t wIdx = static_cast<std::size_t>(tpTuneWeaponIdx_);
            auto& tp = tpWeaponParams_[wIdx];
            WeaponHoldPose& hold = weaponHoldPoses_[wIdx];

            ImGui::TextWrapped(
                "Weapon = entityWorld x Spine2 x T(offset) x R(rot) x scale (rigid child of the upper spine). "
                "Arms are pure FK: each bone's (pitch, yaw) is a local rotation off its rest pose. No IK.");

            ImGui::SeparatorText("Weapon vs Spine2 bone");
            ImGui::DragFloat("Scale", &hold.scale, 0.05f, 0.0001f, 200.0f, "%.3f");
            ImGui::DragFloat3("Offset (R/U/F)", &hold.spineOffset.x, 0.25f, -80.0f, 80.0f, "%.2f");
            ImGui::DragFloat("Rot Step (deg)", &holdRotStepDeg_, 0.5f, 0.1f, 90.0f, "%.1f");
            ImGui::PushButtonRepeat(true);
            auto rotateBy = [&](const glm::vec3& axis, float deltaDeg) {
                hold.spineRotation = glm::normalize(glm::angleAxis(glm::radians(deltaDeg), axis) * hold.spineRotation);
            };
            if (ImGui::Button("Rot X -"))
                rotateBy(glm::vec3{1.0f, 0.0f, 0.0f}, -holdRotStepDeg_);
            ImGui::SameLine();
            if (ImGui::Button("Rot X +"))
                rotateBy(glm::vec3{1.0f, 0.0f, 0.0f}, holdRotStepDeg_);
            ImGui::SameLine();
            if (ImGui::Button("Rot Y -"))
                rotateBy(glm::vec3{0.0f, 1.0f, 0.0f}, -holdRotStepDeg_);
            ImGui::SameLine();
            if (ImGui::Button("Rot Y +"))
                rotateBy(glm::vec3{0.0f, 1.0f, 0.0f}, holdRotStepDeg_);
            ImGui::SameLine();
            if (ImGui::Button("Rot Z -"))
                rotateBy(glm::vec3{0.0f, 0.0f, 1.0f}, -holdRotStepDeg_);
            ImGui::SameLine();
            if (ImGui::Button("Rot Z +"))
                rotateBy(glm::vec3{0.0f, 0.0f, 1.0f}, holdRotStepDeg_);
            ImGui::PopButtonRepeat();
            if (ImGui::Button("Identity rotation"))
                hold.spineRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            ImGui::SameLine();
            ImGui::Checkbox("Show spine-anchor marker", &holdShowDebugMarker_);
            if (spine2JointIdx_ < 0)
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Spine2 not found in rig — weapon hold disabled.");

            ImGui::SeparatorText("Arm FK (pitch, yaw, roll degrees per bone)");
            auto editArm = [&](const char* label, ArmHoldPose& arm) {
                if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
                    return;
                ImGui::PushID(label);
                for (std::size_t i = 0; i < kArmHoldBoneCount; ++i)
                    ImGui::DragFloat3(kArmHoldBoneDisplayNames[i], &arm.boneAngles[i].x, 1.0f, -180.0f, 180.0f, "%.1f");
                static const char* k_fingerNames[kGripPoseFingerCount] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
                static const char* k_jointNames[kGripPoseBonesPerFinger] = {"j1 (root)", "j2", "j3", "j4 (tip)"};
                if (ImGui::TreeNode("Fingers")) {
                    for (std::size_t f = 0; f < kGripPoseFingerCount; ++f) {
                        if (ImGui::TreeNode(k_fingerNames[f])) {
                            for (std::size_t j = 0; j < kGripPoseBonesPerFinger; ++j) {
                                ImGui::PushID(static_cast<int>(j));
                                ImGui::DragFloat3(k_jointNames[j],
                                                  &arm.fingerAngles[GripPose::index(f, j)].x,
                                                  1.0f,
                                                  -180.0f,
                                                  180.0f,
                                                  "%.1f");
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };
            editArm("Right Arm", hold.rightArm);
            editArm("Left Arm", hold.leftArm);

            ImGui::SeparatorText("Procedural Layer Tuning");
            ImGui::DragFloat("Spine Bend Mul", &tp.spineBendMultiplier, 0.05f, 0.0f, 2.0f, "%.2f");
            ImGui::DragFloat("Hip Lean Mul", &tp.hipLeanMultiplier, 0.01f, -0.5f, 0.5f, "%.3f");
            ImGui::DragFloat("Recoil Kick (rad)", &tp.recoilKickRad, 0.005f, 0.0f, 0.5f, "%.3f");
            ImGui::Checkbox("Freeze animations (hold the pose still while tuning)", &tpFreezeAnimations_);

            ImGui::Separator();
            if (ImGui::Button("Reset to defaults")) {
                hold = authoredWeaponHoldPoses_[wIdx];
                tp = getThirdPersonWeaponParams(static_cast<WeaponType>(wIdx));
            }
            ImGui::SameLine();
            if (ImGui::Button("Mirror R -> L")) {
                // Yaw + roll flip sign across the body midline; pitch keeps its sign.
                hold.leftArm.boneAngles = hold.rightArm.boneAngles;
                for (auto& a : hold.leftArm.boneAngles) {
                    a.y = -a.y;
                    a.z = -a.z;
                }
                for (std::size_t i = 0; i < kGripPoseJointCount; ++i) {
                    const glm::vec3 r = hold.rightArm.fingerAngles[i];
                    hold.leftArm.fingerAngles[i] = glm::vec3{r.x, -r.y, -r.z};
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload TOML")) {
                WeaponHoldPose scratch = getWeaponHoldPose(static_cast<WeaponType>(wIdx));
                if (!weaponHoldPosePaths_[wIdx].empty() && loadWeaponHoldPose(weaponHoldPosePaths_[wIdx], scratch))
                    hold = scratch;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save TOML")) {
                if (!weaponHoldPosePaths_[wIdx].empty() && saveWeaponHoldPose(weaponHoldPosePaths_[wIdx], hold)) {
                    std::error_code ec;
                    const auto mtime = std::filesystem::last_write_time(weaponHoldPosePaths_[wIdx], ec);
                    if (!ec)
                        weaponHoldPoseMTimes_[wIdx] = mtime;
                }
            }
            ImGui::TextDisabled("%s", weaponHoldPosePaths_[wIdx].c_str());
        }
        ImGui::End();
    }

    // First-person arm tweaker — independent shoulder/elbow/palm/finger controls.
    if (showFPHandMountUI_) {
        if (ImGui::Begin("FP Arm Tweaker", &showFPHandMountUI_)) {
            const bool equippedRenderable = isRenderableGunType(currentEquippedType_);
            const int equippedWeaponIdx = static_cast<int>(currentEquippedType_);
            ImGui::Text("Equipped: %s", renderableWeaponDisplayName(currentEquippedType_));
            ImGui::Combo("Weapon",
                         &fpHandMountTuneWeaponIdx_,
                         kRenderableWeaponDisplayNames.data(),
                         static_cast<int>(kRenderableWeaponDisplayNames.size()));
            if (equippedRenderable && fpHandMountTuneWeaponIdx_ != equippedWeaponIdx) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Selected weapon is not currently visible.");
                ImGui::SameLine();
                if (ImGui::Button("Select Equipped"))
                    fpHandMountTuneWeaponIdx_ = equippedWeaponIdx;
            }

            auto& mounts = fpHandMountParams_[fpHandMountTuneWeaponIdx_];
            ImGui::DragFloat("FP Arm Scale", &mounts.scale, 0.25f, 1.0f, 120.0f, "%.2f");

            auto setDebugTarget = [&](bool left, HandMountDebugPoint point) {
                handMountDebugTarget_ = HandMountDebugTarget{
                    .space = HandMountDebugSpace::FirstPerson,
                    .weaponIdx = fpHandMountTuneWeaponIdx_,
                    .left = left,
                    .point = point,
                };
            };

            auto drawOffset = [&](const char* label, glm::vec3& offset, bool left, HandMountDebugPoint target) {
                ImGui::PushID(label);
                ImGui::SeparatorText(label);
                ImGui::DragFloat3("Offset", &offset.x, 0.25f, -120.0f, 120.0f, "%.2f");
                if (ImGui::IsItemActive() || ImGui::IsItemHovered())
                    setDebugTarget(left, target);
                ImGui::PopID();
            };

            auto drawMountPoint = [&](const char* label, HandMountPoint& point, bool left, HandMountDebugPoint target) {
                ImGui::PushID(label);
                ImGui::SeparatorText(label);
                ImGui::DragFloat3("Offset", &point.offset.x, 0.25f, -120.0f, 120.0f, "%.2f");
                if (ImGui::IsItemActive() || ImGui::IsItemHovered())
                    setDebugTarget(left, target);
                ImGui::DragFloat3("Rotation", &point.rotationDegrees.x, 1.0f, -180.0f, 180.0f, "%.1f");
                ImGui::PopID();
            };

            auto drawArm = [&](const char* label, FirstPersonArmMountSet& arm, bool left) {
                ImGui::PushID(label);
                if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::PopID();
                    return;
                }
                // Per-finger position/rotation sliders were removed — fingers
                // are now driven purely by the Grip Pose Tweaker (pitch+yaw
                // per joint). Shoulder/elbow/palm still drive the arm IK.
                drawOffset("Shoulder", arm.shoulderOffset, left, HandMountDebugPoint::Shoulder);
                drawOffset("Elbow", arm.elbowOffset, left, HandMountDebugPoint::Elbow);
                drawMountPoint("Palm (weapon grip socket)", arm.palm, left, HandMountDebugPoint::Palm);
                ImGui::PopID();
            };

            drawArm("Right Arm", mounts.rightArm, false);
            drawArm("Left Arm", mounts.leftArm, true);

            ImGui::Separator();
            if (ImGui::Button("Reset Selected Weapon")) {
                mounts = authoredFPHandMountParams_[fpHandMountTuneWeaponIdx_];
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy All FP Settings")) {
                const std::string text = buildFirstPersonHandMountClipboardText(fpHandMountParams_);
                ImGui::SetClipboardText(text.c_str());
            }
        }
        ImGui::End();
    }

    // Weapon spawner model tweaker — per-weapon tuning for world pickup models.
    if (showWeaponSpawnerModelUI_) {
        if (ImGui::Begin("Weapon Spawner Tweaker", &showWeaponSpawnerModelUI_)) {
            ImGui::Combo("Weapon",
                         &spawnerTuneWeaponIdx_,
                         kRenderableWeaponNames.data(),
                         static_cast<int>(kRenderableWeaponNames.size()));

            auto& params = spawnerWeaponParams_[static_cast<std::size_t>(spawnerTuneWeaponIdx_)];

            ImGui::SeparatorText("Translation");
            ImGui::DragFloat("Right", &params.translation.x, 0.5f, -200.0f, 200.0f, "%.1f");
            ImGui::DragFloat("Up", &params.translation.y, 0.5f, -200.0f, 200.0f, "%.1f");
            ImGui::DragFloat("Forward", &params.translation.z, 0.5f, -200.0f, 200.0f, "%.1f");

            ImGui::SeparatorText("Rotation (degrees)");
            ImGui::DragFloat("Yaw", &params.yawOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("Pitch", &params.pitchOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("Roll", &params.rollOffset, 1.0f, -180.0f, 180.0f, "%.1f");

            ImGui::SeparatorText("Scale");
            ImGui::DragFloat("Scale X", &params.scale.x, 0.1f, 0.001f, 200.0f, "%.3f");
            ImGui::DragFloat("Scale Y", &params.scale.y, 0.1f, 0.001f, 200.0f, "%.3f");
            ImGui::DragFloat("Scale Z", &params.scale.z, 0.1f, 0.001f, 200.0f, "%.3f");
            if (ImGui::Button("Uniform from X")) {
                params.scale.y = params.scale.x;
                params.scale.z = params.scale.x;
            }

            ImGui::SeparatorText("Idle Motion");
            ImGui::DragFloat("Spin Deg/Sec", &params.spinDegreesPerSecond, 1.0f, -720.0f, 720.0f, "%.1f");
            ImGui::DragFloat("Bob Amplitude", &params.bobAmplitude, 0.25f, 0.0f, 100.0f, "%.2f");
            ImGui::DragFloat("Bob Hz", &params.bobHz, 0.01f, 0.0f, 10.0f, "%.2f");

            ImGui::Separator();
            if (ImGui::Button("Reset to spawner defaults")) {
                params = defaultSpawnerModelParams(static_cast<WeaponType>(spawnerTuneWeaponIdx_));
            }
            ImGui::SameLine();
            if (ImGui::Button("Save as new defaults")) {
                SDL_Log("[client] spawner weapon %d: scale=(%.3f,%.3f,%.3f) translation=(%.1f,%.1f,%.1f) "
                        "yaw=%.1f pitch=%.1f roll=%.1f spin=%.1f bobAmp=%.2f bobHz=%.2f",
                        spawnerTuneWeaponIdx_,
                        static_cast<double>(params.scale.x),
                        static_cast<double>(params.scale.y),
                        static_cast<double>(params.scale.z),
                        static_cast<double>(params.translation.x),
                        static_cast<double>(params.translation.y),
                        static_cast<double>(params.translation.z),
                        static_cast<double>(params.yawOffset),
                        static_cast<double>(params.pitchOffset),
                        static_cast<double>(params.rollOffset),
                        static_cast<double>(params.spinDegreesPerSecond),
                        static_cast<double>(params.bobAmplitude),
                        static_cast<double>(params.bobHz));
            }
        }
        ImGui::End();
    }

    // Dynamic Lighting debug panel.
    if (showDynLightUI_) {
        ImGui::SetNextWindowPos({10.f, 400.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({280.f, 320.f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Dynamic Lighting", &showDynLightUI_)) {
            ImGui::SeparatorText("Flashlight");
            ImGui::Checkbox("Enable Flashlight", &flashlightEnabled_);
            if (flashlightEnabled_) {
                ImGui::DragFloat("FL Intensity", &flashlightIntensity_, 0.1f, 0.1f, 30.0f, "%.1f");
                ImGui::DragFloat("FL Range", &flashlightRange_, 10.0f, 50.0f, 3000.0f, "%.0f");
                ImGui::DragFloat("FL Offset", &flashlightOffset_, 1.0f, 0.0f, 100.0f, "%.0f");
            }

            ImGui::SeparatorText("Movable Glow Sphere");
            ImGui::Checkbox("Enable Sphere", &movableSphereEnabled_);
            if (movableSphereEnabled_) {
                ImGui::DragFloat("Follow Dist", &sphereFollowDist_, 5.0f, 30.0f, 500.0f, "%.0f");
                ImGui::DragFloat("Sph Intensity", &sphereIntensity_, 0.1f, 0.1f, 30.0f, "%.1f");
                ImGui::DragFloat("Sph Range", &sphereRange_, 10.0f, 50.0f, 3000.0f, "%.0f");
            }

            ImGui::SeparatorText("Bloom Beam");
            ImGui::Checkbox("Enable Beam", &beamEnabled_);
            if (beamEnabled_) {
                ImGui::Text("Offsets: (fwd, up, right) from eye");
                ImGui::DragFloat3("Start Off", &beamStartOff_.x, 1.0f, -500.0f, 500.0f, "%.0f");
                ImGui::DragFloat3("End Off", &beamEndOff_.x, 1.0f, -500.0f, 500.0f, "%.0f");
                ImGui::DragFloat("Radius", &beamRadius_, 0.5f, 0.5f, 50.0f, "%.1f");
                ImGui::ColorEdit3("Beam Color", &beamColor_.x);
                ImGui::DragFloat("Beam Intensity", &beamLightIntensity_, 0.1f, 0.1f, 30.0f, "%.1f");
                ImGui::DragFloat("Beam Lt Range", &beamLightRange_, 10.0f, 50.0f, 3000.0f, "%.0f");
                ImGui::DragFloat("Light Spacing", &beamLightSpacing_, 5.0f, 10.0f, 200.0f, "%.0f");
            }
        }
        ImGui::End();
    }
    if (showHdrDebugUI_) {
        ImGui::SetNextWindowPos({300.f, 400.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({260.f, 120.f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("HDR Debug", &showHdrDebugUI_)) {
            ImGui::SliderFloat("Exposure", &renderer->hdrExposure, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("White Point", &renderer->hdrWhitePoint, 0.1f, 20.0f, "%.2f");
        }
        ImGui::End();
    }
    phaseSnap(phaseStats.imguiMs);

    updateCachedPostMatchResult();

    // Update and render HUD.
    if (hud_.getOutputTexture()) {
        HudGameState hudState{};
        hudState.bindings = &userSettings->inputBindings;
        hudState.activeInputDevice = lastInputDevice_;

        // ── Local player health, armor, alive ──
        registry.view<LocalPlayer, Health>().each([&](const Health& hp) {
            hudState.health = static_cast<int>(hp.health);
            hudState.maxHealth = 100;
            hudState.armor = static_cast<int>(hp.armor);
            hudState.maxArmor = 100;
        });
        registry.view<LocalPlayer, PlayerVisState>().each(
            [&](const PlayerVisState& ps) { hudState.isAlive = !ps.isDead; });
        registry.view<LocalPlayer, AbilityState>().each([&](const AbilityState& ability) {
            hudState.abilityLevelProgress = std::clamp(ability.accumDamage / systems::dmgThreshold, 0.f, 1.f);
            hudState.abilityLevel = ability.level;
            hudState.equipment.primaryAbilityName = abilityName(ability.primary);
            hudState.equipment.secondaryAbilityName = abilityName(ability.secondary);
            hudState.equipment.primaryAbilityAvailable = ability.primary != AbilityType::None;
            hudState.equipment.secondaryAbilityAvailable = ability.secondary != AbilityType::None;
            hudState.equipment.secondaryAbilityMarked =
                ability.secondary == AbilityType::Recall && ability.recallMarkerSet;

            const float primaryDuration = abilities::cooldownFor(ability.primary);
            hudState.equipment.primaryAbilityCharge =
                primaryDuration > 0.0f ? 1.0f - std::clamp(ability.primaryCooldown / primaryDuration, 0.0f, 1.0f)
                                       : 1.0f;
            const float secondaryDuration = abilities::cooldownFor(ability.secondary);
            hudState.equipment.secondaryAbilityCharge =
                secondaryDuration > 0.0f ? 1.0f - std::clamp(ability.secondaryCooldown / secondaryDuration, 0.0f, 1.0f)
                                         : 1.0f;

            if (hasPendingAbilitySelection(ability)) {
                const auto& choices = choicesForPendingSelection(ability);
                hudState.abilitySelection.available = true;
                hudState.abilitySelection.level = std::max(1, ability.level);
                hudState.abilitySelection.slotLabel =
                    pendingAbilitySlot(ability) == AbilitySlot::Primary ? "PRIMARY" : "SECONDARY";
                for (std::size_t i = 0; i < choices.size(); ++i) {
                    hudState.abilitySelection.choices[i].name = abilityName(choices[i]);
                    hudState.abilitySelection.choices[i].description = abilityDescription(choices[i]);
                }
            }
        });
        bool scopeHeld = false;
        registry.view<LocalPlayer, InputSnapshot>().each([&](const InputSnapshot& snap) {
            scopeHeld = snap.scoped;
            hudState.abilitySelection.modifierHeld = snap.abilitySelectHeld;
        });

        // ── Weapon / ammo ──
        registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
            const GunInstance& gun = getEquippedGun(ws);
            hudState.primaryWeaponId = static_cast<int>(getSlot(ws, WeaponSlot::PRIMARY).type);
            hudState.secondarySlotWeaponId = static_cast<int>(getSlot(ws, WeaponSlot::SECONDARY).type);
            hudState.ammoClip = gun.currentMagAmmo;
            hudState.ammoReserve = gun.totalAmmo;
            hudState.weaponId = static_cast<int>(gun.type);
            hudState.railgunScoped = scopeHeld && gun.type == WeaponType::RailGun;
            hudState.railgunChargeTime = gun.type == WeaponType::RailGun ? gun.chargeTime : 0.f;
            // Mag capacity comes straight from the static WeaponConfig table,
            // so the "47/30" rifle bug (HUD hardcoded /30 vs. real /50) is
            // gone — the HUD reads exactly what gameplay says.
            hudState.magCapacity = getWeaponConfig(gun.type).magazineSize;
            hudState.isReloading = gun.isReloading;
            const WeaponConfig& config = getWeaponConfig(gun.type);
            if (gun.isReloading && config.reloadTime > 0.f) {
                hudState.reloadProgress = 1.0f - (gun.reloadTime / config.reloadTime);
            }
        });

        // ── Emote wheel (radial selection driven by the input sampler) ──
        hudState.emoteWheel.open = systems::emoteWheelOpen;
        hudState.emoteWheel.selectedIndex = systems::emoteWheelSelection;

        // ── Round timer ──
        hudState.roundTimeRemaining = countdownTimer;
        hudState.currentPhase = currentMatchPhase;
        hudState.forceScoreboardOpen = false;

        // ── Kill feed: convert KillFeedEvent → HudKillFeedEntry ──
        ClientId localClientId{-1};
        registry.view<LocalPlayer, ClientId>().each([&](const ClientId& cid) { localClientId = cid; });
        hudState.matchWon =
            currentMatchPhase == MatchPhase::FINISHED && localClientId.value != -1 && currentWinnerId == localClientId;

        thread_local std::vector<HudKillFeedEntry> hudKillEntries;
        hudKillEntries.clear();
        // Only pass each event once — sentToHud flag prevents duplicates.
        for (auto& evt : killFeed) {
            if (!evt.sentToHud) {
                evt.sentToHud = true;
                HudKillFeedEntry entry;
                char nameBuf[32];
                if (localClientId.value != -1 && evt.killerId == localClientId)
                    entry.killerName = "You";
                else
                    entry.killerName = lookupPlayerName(registry, evt.killerId, nameBuf, sizeof(nameBuf));
                if (localClientId.value != -1 && evt.victimId == localClientId)
                    entry.victimName = "You";
                else
                    entry.victimName = lookupPlayerName(registry, evt.victimId, nameBuf, sizeof(nameBuf));
                hudKillEntries.push_back(entry);
            }
        }
        hudState.killFeedEvents = hudKillEntries;

        // ── Hit confirms: feed from hitmarkerTimer_ (set by replicated particle events) ──
        thread_local std::vector<HudHitConfirm> hudHitConfirms;
        hudHitConfirms.clear();
        if (hitmarkerTimer_ > 0.2f) { // just triggered (timer starts at 0.25)
            hudHitConfirms.push_back({hitmarkerIsHeadshot_, false, hitmarkerShieldBreak_});
        }
        hudState.hitConfirms = hudHitConfirms;

        // ── Shotgun blast: age the most recent completed blast and stage it
        //    for the ShotgunPelletWidget. The widget detects "fresh" by
        //    comparing secondsSinceFire to its own cached age.
        if (lastShotgunBlast_.valid) {
            lastShotgunBlast_.secondsSinceFire += frameTime;
        }
        hudState.latestShotgunBlast = lastShotgunBlast_;

        // ── Vignette: detect health/armor deltas for damage & shield break ──
        {
            float curHealth = 100.f, curArmor = 100.f;
            registry.view<LocalPlayer, Health>().each([&](const Health& hp) {
                curHealth = hp.health;
                curArmor = hp.armor;
            });

            const float healthLost = prevHealth_ - curHealth;
            const float armorLost = prevArmor_ - curArmor;
            const float totalLost = std::max(0.f, healthLost) + std::max(0.f, armorLost);

            hudState.armorBroke = (prevArmor_ > 0.f && curArmor <= 0.f);

            // Red vignette on damage — suppressed when the shield breaks so
            // the blue shield-break vignette plays alone.
            if (totalLost > 0.f && !hudState.armorBroke) {
                hudState.tookDamage = true;
                hudState.damageIntensity = std::clamp(totalLost / 100.f, 0.f, 1.f);
            }

            prevHealth_ = curHealth;
            prevArmor_ = curArmor;
        }

        // ── Scoreboard: all players ──
        thread_local std::vector<HudTeamMemberStatus> hudAllPlayers;
        hudAllPlayers.clear();
        registry.view<ClientId, Health, PlayerVisState>().each(
            [&](entt::entity ent, const ClientId& cid, const Health& hp, const PlayerVisState& ps) {
                HudTeamMemberStatus status;
                if (localClientId.value != -1 && cid == localClientId) {
                    status.name = "You";
                } else if (const auto* pn = registry.try_get<PlayerName>(ent); pn != nullptr && !pn->empty()) {
                    status.name = pn->c_str();
                } else {
                    char buf[16];
                    SDL_snprintf(buf, sizeof(buf), "Player #%d", cid.value);
                    status.name = buf;
                }
                status.health = static_cast<int>(hp.health);
                status.isAlive = !ps.isDead;
                if (const auto* pms = registry.try_get<PlayerMatchStats>(ent)) {
                    status.kills = pms->kills;
                    status.deaths = pms->deaths;
                    // Per-player ping: the server stamps each client's
                    // self-reported RTT onto their PlayerMatchStats.rttMs
                    // every tick (see ServerGame::updateLagCompTargets),
                    // which then rides the existing snapshot stream to
                    // every connected client-> Reading from this row's
                    // own component instead of `client->getNetStats()`
                    // means every scoreboard row shows the right
                    // player's ping — not the local viewer's ping
                    // duplicated across every line.
                    status.ping = static_cast<int>(pms->rttMs);
                }
                hudAllPlayers.push_back(status);
            });
        // For now all players go into allies (no team system exists).
        hudState.allies = hudAllPlayers;
        hudState.allyScore = 0;
        hudState.enemyScore = 0;

        // ── Minimap: local player + all other player positions ──
        registry.view<LocalPlayer, Position, InputSnapshot>().each(
            [&](const Position& pos, const InputSnapshot& input) {
                hudState.localPlayerX = pos.value.x;
                hudState.localPlayerZ = pos.value.z;
                hudState.localPlayerYaw = input.yaw;
            });

        thread_local std::vector<HudMinimapDot> hudMinimapDots;
        hudMinimapDots.clear();
        registry.view<ClientId, Position, PlayerVisState>().each(
            [&](const ClientId& cid, const Position& pos, const PlayerVisState& ps) {
                if (ps.isDead)
                    return;
                if (localClientId.value != -1 && cid == localClientId)
                    return; // Skip local player (always drawn at center).
                if (ps.crouching)
                    return; // Crouching hides you from enemy radar.
                hudMinimapDots.push_back({pos.value.x, pos.value.z});
            });
        hudState.enemyDots = hudMinimapDots;

        // ── Screen dimensions ──
        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        hudState.screenW = static_cast<float>(winW);
        hudState.screenH = static_cast<float>(winH);

        // ── Killcam killer box (red AABB framing the killer while dead) ──
        // The killer entity + AABB were resolved during camera resolution.
        hudState.killerBox.valid = killcamActive_;
        hudState.killerBox.center = killcamKillerCenter_;
        hudState.killerBox.halfExtents = killcamKillerHalf_;
        hudState.killerBox.name = killcamKillerName_;

        // ── Floating damage numbers ──
        thread_local std::vector<HudDamageNumber> hudDamageNumbers;
        hudDamageNumbers.clear();
        for (const auto& pdn : pendingDamageNumbers_) {
            hudDamageNumbers.push_back(
                {pdn.pos.x, pdn.pos.y, pdn.pos.z, static_cast<int>(pdn.damage + 0.5f), pdn.headshot, pdn.shielded});
        }
        pendingDamageNumbers_.clear();
        hudState.damageNumbers = hudDamageNumbers;

        // ── Damage accumulator ──
        accumResetTimer_ -= frameTime;
        if (accumResetTimer_ <= 0.f) {
            accumTarget_ = entt::null;
            accumTotal_ = 0;
        }
        hudState.damageAccum.total = accumTotal_;
        // HUD palette: headshot=primary, shield=tertiary, hp=white.
        if (accumLastHitType_ == 2)
            hudState.damageAccum.color = voidfall::k_primary;
        else if (accumLastHitType_ == 1)
            hudState.damageAccum.color = voidfall::k_tertiary;
        else
            hudState.damageAccum.color = voidfall::k_health;

        // ── View-projection matrix for world→screen projection ──
        hudState.viewProj = renderer->getCamera().getViewProjectionMatrix();

        // ── Weapon pickup prompt ──
        // Mirror the server pickup-detection rule (range + look cone via
        // PickupGeometry) so the pickup hint appears exactly when pressing
        // the configured binding would actually grant the weapon. Cheap O(N)
        // sweep across world weapon spawners and dropped weapons.
        {
            glm::vec3 eye{0.f};
            glm::vec3 viewFwd{0.f, 0.f, 1.f};
            bool haveLocal = false;
            registry.view<LocalPlayer, Position, CollisionShape, InputSnapshot, PlayerVisState>().each(
                [&](const Position& pos,
                    const CollisionShape& shape,
                    const InputSnapshot& input,
                    const PlayerVisState& pvis) {
                    const float pickupEyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
                    eye = pos.value + glm::vec3{0.f, shape.halfExtents.y * 0.77f * pickupEyeDir, 0.f};
                    const float cp = std::cos(input.pitch);
                    viewFwd = glm::normalize(glm::vec3{
                        std::sin(input.yaw) * cp,
                        -std::sin(input.pitch),
                        std::cos(input.yaw) * cp,
                    });
                    haveLocal = true;
                });

            if (haveLocal && hudState.isAlive) {
                float bestDistSq = systems::k_pickupRange * systems::k_pickupRange + 1.f;
                auto consider = [&](const glm::vec3& itemPos, int weaponId) {
                    if (!systems::isPlayerLookingAtPickup(eye, viewFwd, itemPos))
                        return;
                    const glm::vec3 toW = itemPos - eye;
                    const float distSq = glm::dot(toW, toW);
                    if (distSq < bestDistSq) {
                        bestDistSq = distSq;
                        hudState.pickupAvailable = true;
                        hudState.pickupWeaponId = weaponId;
                    }
                };
                registry.view<Position, WeaponSpawner>().each([&](const Position& spPos, const WeaponSpawner& sp) {
                    if (!sp.hasWeapon)
                        return;
                    consider(spPos.value, static_cast<int>(sp.type));
                });
                registry.view<Position, DroppedWeapon>().each([&](const Position& dpPos, const DroppedWeapon& dw) {
                    consider(dpPos.value, static_cast<int>(dw.type));
                });
            }
        }

        // ── Voidfall HUD: KDA from local player's PlayerMatchStats ──
        registry.view<LocalPlayer, PlayerMatchStats>().each([&](const PlayerMatchStats& pms) {
            hudState.kda.kills = pms.kills;
            hudState.kda.deaths = pms.deaths;
            // No assist tracking yet — leaving 0 until a future
            // PlayerMatchStats.assists field is added & replicated.
            hudState.kda.assists = 0;
        });

        // ── Voidfall HUD: equipment cooldowns ──
        // Grapple: PlayerSimState lives server-side, but the same cooldown
        // timer is mirrored to the client via PlayerVisState.grappleActive
        // (active vs. cooled-down).  Without per-tick remaining, fake a
        // smooth fill by holding 0 → 1 over k_grappleCooldown after each
        // active-pull ends.  Falls back to "ready" when no PlayerSimState
        // is reachable on the local entity.
        {
            float grappleCharge = 1.f;
            registry.view<LocalPlayer, PlayerVisState>().each([&](const PlayerVisState& vis) {
                // While the cable is taut, the cooldown hasn't started yet.
                if (vis.grappleActive)
                    grappleCharge = 0.f;
            });
            // If the simulation timer is reachable (server-side replicated
            // path), use the precise value.  PlayerSimState is only present
            // on the server, so on the client we approximate by ramping the
            // value back up over the design's `k_grappleCooldown` once the
            // cable becomes inactive.  Use a Game.cpp-local timer:
            static float s_grappleSinceRelease = 1e9f; // big = idle
            static bool s_grappleWasActive = false;
            bool nowActive = false;
            registry.view<LocalPlayer, PlayerVisState>().each(
                [&](const PlayerVisState& vis) { nowActive = vis.grappleActive; });
            if (nowActive)
                s_grappleSinceRelease = 0.f;
            else if (s_grappleWasActive && !nowActive)
                s_grappleSinceRelease = 0.f;
            else
                s_grappleSinceRelease += frameTime;
            s_grappleWasActive = nowActive;
            if (nowActive)
                grappleCharge = 0.f;
            else
                grappleCharge = std::clamp(s_grappleSinceRelease / tms::k_grappleCooldown, 0.f, 1.f);
            hudState.equipment.grappleCharge = grappleCharge;

            registry.view<LocalPlayer, GrenadeState>().each([&](const GrenadeState& grenades) {
                const WeaponType selected = grenades.selected;
                hudState.equipment.grenadeName = grenadeTypeName(selected);
                hudState.equipment.grenadeCount = grenadeAmmo(grenades, selected);
                const float cooldown = getGrenadeConfig(selected).throwCooldown;
                hudState.equipment.grenadeCharge =
                    cooldown > 0.0f ? 1.0f - std::clamp(grenades.cooldown / cooldown, 0.0f, 1.0f) : 1.0f;
                hudState.grenadeRadial.selectedIndex = static_cast<int>(grenadeTypeIndex(selected));

                for (std::size_t i = 0; i < kGrenadeTypes.size() && i < hudState.grenadeRadial.items.size(); ++i) {
                    const WeaponType type = kGrenadeTypes[i];
                    const int ammo = grenadeAmmo(grenades, type);
                    hudState.grenadeRadial.items[i].name = grenadeTypeName(type);
                    hudState.grenadeRadial.items[i].count = ammo;
                    hudState.grenadeRadial.items[i].available = ammo > 0;
                }
            });

            // Tactical: not implemented in ECS yet; show as ready with a
            // single charge so the slot still renders correctly.
            hudState.equipment.tacticalCount = 1;
            hudState.equipment.tacticalCharge = 1.f;
        }

        // ── Voidfall HUD: world-space enemy HP bars ──
        thread_local std::vector<HudWorldEnemy> hudWorldEnemies;
        thread_local std::vector<std::string> hudWorldEnemyNames;
        hudWorldEnemies.clear();
        hudWorldEnemyNames.clear();
        // Reserve up-front so subsequent push_backs don't invalidate the
        // string pointers we hand off to HudWorldEnemy::name (we copy the
        // strings, but the reservation also avoids reallocation jitter).
        hudWorldEnemyNames.reserve(16);
        registry.view<ClientId, Position, CollisionShape, Health, PlayerVisState>().each(
            [&](entt::entity ent,
                const ClientId& cid,
                const Position& pos,
                const CollisionShape& shape,
                const Health& hp,
                const PlayerVisState& pvis) {
                if (localClientId.value != -1 && cid == localClientId)
                    return;
                if (pvis.isDead)
                    return;
                HudWorldEnemy we;
                // Place the floating bar slightly above the enemy capsule.
                const float headDir = pvis.gravityFlipped ? -1.0f : 1.0f;
                we.worldX = pos.value.x;
                we.worldY = pos.value.y + (shape.halfExtents.y + 18.f) * headDir;
                we.worldZ = pos.value.z;
                if (const auto* pn = registry.try_get<PlayerName>(ent); pn != nullptr && !pn->empty()) {
                    hudWorldEnemyNames.emplace_back(pn->c_str());
                } else {
                    char buf[24];
                    SDL_snprintf(buf, sizeof(buf), "PLR-%02d", cid.value);
                    hudWorldEnemyNames.emplace_back(buf);
                }
                we.name = hudWorldEnemyNames.back();
                we.health = static_cast<int>(hp.health);
                we.maxHealth = 100;
                we.armor = static_cast<int>(hp.armor);
                we.maxArmor = 100;
                we.isAlive = !pvis.isDead;

                // Occlusion: cast from the camera eye toward the enemy's body
                // center. If static world geometry is hit before reaching the
                // enemy, a wall is in the way — flag it so the HUD hides the
                // floating bar/name (no see-through-walls wallhack).
                const glm::vec3 bodyCenter = pos.value + glm::vec3{0.f, shape.halfExtents.y * 0.5f * headDir, 0.f};
                const glm::vec3 toEnemy = bodyCenter - cachedEye_;
                const float distToEnemy = glm::length(toEnemy);
                if (distToEnemy > 1.f) {
                    const physics::HitscanHit losHit =
                        physics::raycastWorld(cachedEye_, toEnemy / distToEnemy, physics::activeWorld());
                    we.occluded = losHit.hit && losHit.distance < distToEnemy - 2.f;
                }

                hudWorldEnemies.push_back(we);
            });
        hudState.worldEnemies = hudWorldEnemies;

        // ── Voidfall HUD: secondary weapon snapshot ──
        registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
            // On PRIMARY → show SECONDARY in the panel; on anything else (SECONDARY or
            // a non-gun slot like GRENADE) → show PRIMARY, the player's main weapon.
            const WeaponSlot otherSlot =
                (ws.current == WeaponSlot::PRIMARY) ? WeaponSlot::SECONDARY : WeaponSlot::PRIMARY;
            const GunInstance& secGun = getSlot(ws, otherSlot);
            if (static_cast<int>(secGun.type) >= 0) {
                hudState.secondaryWeaponId = static_cast<int>(secGun.type);
                hudState.secondaryClip = secGun.currentMagAmmo;
                hudState.secondaryReserve = secGun.totalAmmo;
                hudState.secondaryMagCapacity = getWeaponConfig(secGun.type).magazineSize;
                // Keybind label tracks the *inactive* slot so the sub-row
                // always advertises the right swap key (`1` while holding
                // SECONDARY, `2` while holding PRIMARY).
                hudState.secondaryKeybind = static_cast<int>(otherSlot) + 1;
            }
        });

        // ── Voidfall HUD: pickup notifications (slide-in) ──
        // Detect new weapons or ammo growth on the local player vs. the
        // previous frame's snapshot.  This means picking up a Rifle from a
        // spawner — or any other source that mutates a WeaponState slot —
        // surfaces in the right-side feed.
        {
            int curPrim = -1;
            int curSec = -1;
            int curReserve = 0;
            registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
                curPrim = static_cast<int>(getSlot(ws, WeaponSlot::PRIMARY).type);
                curSec = static_cast<int>(getSlot(ws, WeaponSlot::SECONDARY).type);
                const GunInstance& gun = getEquippedGun(ws);
                curReserve = gun.currentMagAmmo + gun.totalAmmo;
            });

            auto pushPickup = [&](const std::string& label, int qty) {
                pendingPickupNotifications_.push_back({label, qty});
            };

            if (prevPrimaryWeaponType_ != -1 && curPrim != prevPrimaryWeaponType_ && curPrim >= 0) {
                const char* names[] = {"RIFLE", "ROCKET", "RAILGUN", "ENERGY"};
                const char* nm = (curPrim >= 0 && curPrim < 4) ? names[curPrim] : "WEAPON";
                pushPickup(nm, 1);
            }
            if (prevSecondaryWeaponType_ != -1 && curSec != prevSecondaryWeaponType_ && curSec >= 0) {
                const char* names[] = {"RIFLE", "ROCKET", "RAILGUN", "ENERGY"};
                const char* nm = (curSec >= 0 && curSec < 4) ? names[curSec] : "WEAPON";
                pushPickup(nm, 1);
            }
            if (prevAmmoReserve_ >= 0 && curReserve > prevAmmoReserve_ + 5) {
                pushPickup("AMMO", curReserve - prevAmmoReserve_);
            }
            prevPrimaryWeaponType_ = curPrim;
            prevSecondaryWeaponType_ = curSec;
            prevAmmoReserve_ = curReserve;

            // Ship the pending list and drain — each notification is a
            // one-shot event; the widget owns its lifetime.
            hudState.pickupNotifications = pendingPickupNotifications_;
            hudState.popupMessages = pendingPopupMessages_;
        }

        // ── Voidfall HUD: gravity direction ──
        {
            bool flipped = false;
            registry.view<LocalPlayer, PlayerVisState>().each(
                [&](const PlayerVisState& vis) { flipped = vis.gravityFlipped; });
            hudState.gravityDirection = flipped ? 2 : 0; // 0 = down, 2 = up
        }

        // ── Voidfall HUD: match-header info ──
        if (currentMatchPhase == MatchPhase::IN_PROGRESS || currentMatchPhase == MatchPhase::FINISHED) {
            matchElapsedSeconds_ += frameTime;
        } else {
            matchElapsedSeconds_ = 0.f;
        }
        hudState.matchInfo.elapsedSeconds = matchElapsedSeconds_;
        hudState.matchInfo.fragTarget = 30; // matches design default; replaced once the server replicates a target.
        hudState.matchInfo.valid =
            (currentMatchPhase == MatchPhase::IN_PROGRESS || currentMatchPhase == MatchPhase::FINISHED);

        for (auto& msg : chatMessages_)
            msg.ageSeconds += frameTime;
        if (!chatOpen_) {
            chatMessages_.erase(std::remove_if(chatMessages_.begin(),
                                               chatMessages_.end(),
                                               [](const HudChatMessage& msg) { return msg.ageSeconds > 30.0f; }),
                                chatMessages_.end());
        }
        hudState.chat.open = chatOpen_;
        hudState.chat.draft = chatDraft_;
        hudState.chat.messages = chatMessages_;

        voiceSpeakers_.clear();
        for (const auto& speaker : voiceChat_.speaking()) {
            HudVoiceSpeaker hudSpeaker;
            char nameBuf[32];
            hudSpeaker.senderName = lookupPlayerName(registry, speaker.speaker, nameBuf, sizeof(nameBuf));
            voiceSpeakers_.push_back(std::move(hudSpeaker));
        }
        hudState.voiceSpeakers = voiceSpeakers_;

        hud_.update(frameTime, hudState);
        hud_.render();

        // Drain pickup notifications now that the HUD has consumed them.
        pendingPickupNotifications_.clear();
        pendingPopupMessages_.clear();
    }
    phaseSnap(phaseStats.hudMs);

    const PauseMenuResult pauseResult = pauseMenu.render(*userSettings, userSettingsPath_);
    if (pauseResult.settingsApplied) {
        mouseSensitivity = userSettings->mouseSensitivity;
        horizontalFovDegrees = userSettings->horizontalFovDegrees;
        renderer->mainHorizontalFovDegrees = horizontalFovDegrees;
        clearGameplayInputForChat();
    }
    if (pauseResult.resumeGame) {
        pauseMenu.close();
        mouseCaptured = true;
        SDL_SetWindowRelativeMouseMode(window, true);
        float dx = 0.0f;
        float dy = 0.0f;
        SDL_GetRelativeMouseState(&dx, &dy);
        clearGameplayInputForChat();
    }
    if (pauseResult.exitToDesktop)
        return SDL_APP_SUCCESS;
    if (pauseResult.returnToMainMenu) {
        returnToMainMenuRequested_ = true;
    }
    phaseSnap(phaseStats.pauseMenuMs);

    debugUI.render();
    if (collectPerf) {
        if (ImDrawData* drawData = ImGui::GetDrawData()) {
            phaseStats.imguiDrawLists = static_cast<std::uint32_t>(drawData->CmdListsCount);
            phaseStats.imguiVertices = static_cast<std::uint32_t>(drawData->TotalVtxCount);
            phaseStats.imguiIndices = static_cast<std::uint32_t>(drawData->TotalIdxCount);
        }
    }

    // Smooth camera roll interpolation (degrees → radians).
    // Uses shortest-path delta so that a 0→180° gravity flip rolls the
    // correct direction instead of going the long way around.
    {
        const float k_targetRad = glm::radians(targetRoll);
        const float k_speed = 6.0f; // interpolation speed (lower = smoother gravity flip roll)
        // Compute shortest-path delta, wrapping to [-π, π].
        float delta = k_targetRad - currentCameraRoll_;
        delta = std::remainder(delta, glm::two_pi<float>());
        currentCameraRoll_ += delta * std::min(1.0f, k_speed * frameTime);
        // Wrap current roll to [-π, π] to prevent drift.
        currentCameraRoll_ = std::remainder(currentCameraRoll_, glm::two_pi<float>());
        // Kill tiny residual to avoid permanent micro-tilt when near zero.
        if (std::abs(currentCameraRoll_) < 0.001f && std::abs(k_targetRad) < 0.001f)
            currentCameraRoll_ = 0.0f;
    }
    phaseSnap(phaseStats.imguiRenderMs);
    renderer->mainHorizontalFovDegrees = horizontalFovDegrees;
    {
        // Drive scope zoom from PlayerVisState::ads (set in MovementSystem whenever
        // RMB is held with a charge weapon equipped). 1.5x divides the horizontal
        // FOV — e.g. 90° → 60° — by the standard game-engine "Nx scope" convention.
        float zoom = 1.0f;
        registry.view<LocalPlayer, PlayerVisState>().each(
            [&](const PlayerVisState& vis) { zoom = vis.ads ? 1.5f : 1.0f; });
        renderer->scopeZoom = zoom;
    }
    renderer->drawFrame(renderEye, renderYaw, renderPitch, currentCameraRoll_);
    phaseSnap(phaseStats.drawFrameMs);
    if (collectPerf) {
        phaseStats.drawAcquireMs = renderer->getLastAcquireMs();
        phaseStats.drawRecordMs = renderer->getLastRecordMs();
        phaseStats.drawSubmitMs = renderer->getLastSubmitMs();

        phaseStats.physicsTicks = static_cast<std::uint32_t>(ticksThisFrame);
        phaseStats.tickCount = static_cast<std::uint32_t>(tickCount);
        phaseStats.clientPredictTick = clientPredictTick;
        if (phaseStats.serverAckedClientTick == 0)
            phaseStats.serverAckedClientTick = client->getServerAckedClientTick();
        phaseStats.accumulatorMs = accumulator * 1000.0f;
        phaseStats.measuredPhysicsHz = measuredPhysicsHz;
        phaseStats.fpsCurrent = statsFPSCurrent;
        phaseStats.fps1pLow = statsFPS1pLow;
        phaseStats.fps5pLow = statsFPS5pLow;

        phaseStats.playerEntities = static_cast<std::uint32_t>(registry.view<ClientId, PlayerVisState>().size_hint());
        phaseStats.localPlayers = static_cast<std::uint32_t>(registry.storage<LocalPlayer>().size());
        phaseStats.renderableEntities = static_cast<std::uint32_t>(registry.storage<Renderable>().size());
        phaseStats.projectileEntities = static_cast<std::uint32_t>(registry.storage<Projectile>().size());
        phaseStats.fireFields = static_cast<std::uint32_t>(registry.storage<FireField>().size());

        phaseStats.impactParticles = particleSystem.impactCount();
        phaseStats.tracerParticles = particleSystem.tracerCount();
        phaseStats.ribbonVertices = particleSystem.ribbonVertexCount();
        phaseStats.hitscanBeams = particleSystem.hitscanBeamCount();
        phaseStats.arcVertices = particleSystem.arcVertexCount();
        phaseStats.smokeParticles = particleSystem.smokeCount();
        phaseStats.decals = particleSystem.decalCount();

        phaseStats.audioSourcesActive = sfxSystem.activeSourceCount();
        phaseStats.voiceSourcesActive = sfxSystem.activeVoiceSourceCount();
        const auto& audioStats = sfxSystem.audioStats();
        phaseStats.audioEventsPosted = audioStats.postedEvents;
        phaseStats.audioCommandsGenerated = audioStats.commandsGenerated;
        const auto& sfxStats = sfxSystem.sfxStats();
        phaseStats.audioSourcesStarted = sfxStats.sourcesStarted;
        phaseStats.audioDroppedByCooldown = sfxStats.droppedByCooldown;
        phaseStats.audioDroppedByLimit = sfxStats.droppedByLimit;
        phaseStats.audioStolenSources = sfxStats.stolenSources;

        const NetworkStats& netStats = client->getNetStats();
        phaseStats.rttMs = netStats.rttMs;
        phaseStats.avgRttMs = netStats.avgRttMs;
        phaseStats.recvKBps = netStats.recvBytesPerSec / 1024.0f;
        phaseStats.sendKBps = netStats.sendBytesPerSec / 1024.0f;
        phaseStats.registryUpdateKB = static_cast<float>(netStats.registryUpdateSize) / 1024.0f;

        const physics::perf::FrameStats physicsPerf = physics::perf::snapshot();
        phaseStats.perfMovementCalls = physicsPerf.movementCalls;
        phaseStats.perfMovementPlayers = physicsPerf.movementPlayers;
        phaseStats.perfCollisionCalls = physicsPerf.collisionCalls;
        phaseStats.perfCollisionPlayers = physicsPerf.collisionPlayers;
        phaseStats.perfKccCalls = physicsPerf.kccCalls;
        phaseStats.perfKccBumpHits = physicsPerf.kccBumpHits;
        phaseStats.perfKccCaIterations = physicsPerf.kccCaIterations;
        phaseStats.perfKccSweepHits = physicsPerf.kccSweepHits;
        phaseStats.perfWallDetectCalls = physicsPerf.wallDetectCalls;
        phaseStats.perfWallMeshProbes = physicsPerf.wallMeshProbes;
        phaseStats.perfWallMeshProbeMeshes = physicsPerf.wallMeshProbeMeshes;
        phaseStats.perfWallSphereFallbacks = physicsPerf.wallSphereFallbacks;
        phaseStats.perfWallAttachmentCalls = physicsPerf.wallAttachmentCalls;
        phaseStats.perfWallAttachmentMeshes = physicsPerf.wallAttachmentMeshes;
        phaseStats.perfWallDetectSkippedByGate = physicsPerf.wallDetectSkippedByGate;
        phaseStats.perfWallAttachmentPrevTriangleHits = physicsPerf.wallAttachmentPrevTriangleHits;
        phaseStats.perfWallAttachmentNeighborHits = physicsPerf.wallAttachmentNeighborHits;
        phaseStats.perfWallAttachmentBroadphaseFallbacks = physicsPerf.wallAttachmentBroadphaseFallbacks;
        phaseStats.perfStaticBroadphaseQueries = physicsPerf.staticBroadphaseQueries;
        phaseStats.perfStaticBroadphaseMeshes = physicsPerf.staticBroadphaseMeshes;
        phaseStats.perfSweepAabbAllCalls = physicsPerf.sweepAabbAllCalls;
        phaseStats.perfSweepCapsuleAllCalls = physicsPerf.sweepCapsuleAllCalls;
        phaseStats.perfSweepCapsuleTriMeshCalls = physicsPerf.sweepCapsuleTriMeshCalls;
        phaseStats.perfSweepCapsuleTriMeshNodes = physicsPerf.sweepCapsuleTriMeshNodes;
        phaseStats.perfSweepCapsuleTriMeshTris = physicsPerf.sweepCapsuleTriMeshTris;
        phaseStats.perfDeepestCapsuleCalls = physicsPerf.deepestCapsuleCalls;
        phaseStats.perfDeepestCapsuleTriMeshCalls = physicsPerf.deepestCapsuleTriMeshCalls;
        phaseStats.perfDeepestCapsuleTriMeshNodes = physicsPerf.deepestCapsuleTriMeshNodes;
        phaseStats.perfDeepestCapsuleTriMeshTris = physicsPerf.deepestCapsuleTriMeshTris;
        phaseStats.perfClosestPointMeshCalls = physicsPerf.closestPointMeshCalls;
        phaseStats.perfClosestPointMeshNodes = physicsPerf.closestPointMeshNodes;
        phaseStats.perfClosestPointMeshTris = physicsPerf.closestPointMeshTris;
        phaseStats.perfClosestPointTriangleCalls = physicsPerf.closestPointTriangleCalls;
        phaseStats.perfClosestPointWallProbeCalls = physicsPerf.closestPointWallProbeCalls;
        phaseStats.perfClosestPointWallProbeNodes = physicsPerf.closestPointWallProbeNodes;
        phaseStats.perfClosestPointWallProbeTris = physicsPerf.closestPointWallProbeTris;
        phaseStats.perfClosestPointWallAttachmentCalls = physicsPerf.closestPointWallAttachmentCalls;
        phaseStats.perfClosestPointWallAttachmentNodes = physicsPerf.closestPointWallAttachmentNodes;
        phaseStats.perfClosestPointWallAttachmentTris = physicsPerf.closestPointWallAttachmentTris;
    }

    if (collectPerf) {
        const Uint64 endTick = SDL_GetPerformanceCounter();
        phaseStats.cpuFrameMs = static_cast<float>(endTick - k_now) * 1000.0f / static_cast<float>(k_perfFreq);
    }

    if (benchActive_) {
        // Only retain frames that match what benchFrameTimesMs_ collects (post-warmup).
        const float benchElapsedNow = static_cast<float>(k_now - benchStartTime_) / static_cast<float>(k_perfFreq);
        if (benchElapsedNow >= k_benchWarmupSeconds && phaseStats.cpuFrameMs > 0.0f && phaseStats.cpuFrameMs < 250.0f)
            benchFrameStats_.push_back(phaseStats);
    }

    if (limitFPSToMonitor != prevLimitFPS)
        applyFrameRateLimit();

    // Software frame limiter: sleep + spin-wait when targeting above monitor refresh.
    const Uint64 limiterStart = collectPerf ? SDL_GetPerformanceCounter() : 0;
    if (softLimitPeriod != 0) {
        const Uint64 perfFreq = SDL_GetPerformanceFrequency();
        const Uint64 now = SDL_GetPerformanceCounter();
        if (now < softLimitNextFrame) {
            const Sint64 sleepMs = static_cast<Sint64>((softLimitNextFrame - now) * 1000 / perfFreq) - 1;
            if (sleepMs > 0)
                SDL_Delay(static_cast<Uint32>(sleepMs));
            // Spin-wait for remaining sub-millisecond precision.
            while (SDL_GetPerformanceCounter() < softLimitNextFrame) {
            }
        }
        softLimitNextFrame = SDL_GetPerformanceCounter() + softLimitPeriod;
    }
    if (collectPerf) {
        const Uint64 limiterEnd = SDL_GetPerformanceCounter();
        phaseStats.frameLimiterMs =
            static_cast<float>(limiterEnd - limiterStart) * 1000.0f / static_cast<float>(k_perfFreq);
        perfRecorder_.record(phaseStats);
    }

    return SDL_APP_CONTINUE;
}

bool Game::shouldReturnToLobby() const
{
    return returnToLobbyRequested;
}

std::optional<PostMatchResult> Game::consumePostMatchResult()
{
    if (!returnToLobbyRequested || !cachedPostMatchResult_)
        return std::nullopt;

    auto result = std::move(cachedPostMatchResult_);
    cachedPostMatchResult_.reset();
    return result;
}

bool Game::consumeReturnToMainMenu()
{
    if (!returnToMainMenuRequested_)
        return false;

    returnToMainMenuRequested_ = false;
    return true;
}

bool Game::consumeServerShutdownNotice()
{
    if (!serverShutdownNoticeRequested_)
        return false;

    serverShutdownNoticeRequested_ = false;
    return true;
}

void Game::updateCachedPostMatchResult()
{
    if (currentMatchPhase != MatchPhase::FINISHED)
        return;

    ClientId localClientId{-1};
    registry.view<LocalPlayer, ClientId>().each([&](const ClientId& cid) { localClientId = cid; });

    PostMatchResult result;
    result.winnerId = currentWinnerId.value;
    result.won = localClientId.value != -1 && currentWinnerId == localClientId;

    registry.view<ClientId, Health, PlayerVisState>().each(
        [&](entt::entity ent, const ClientId& cid, const Health&, const PlayerVisState&) {
            PostMatchScoreRow row;
            row.clientId = cid.value;
            row.isLocal = localClientId.value != -1 && cid == localClientId;

            if (const auto* pn = registry.try_get<PlayerName>(ent); pn != nullptr && !pn->empty()) {
                row.name = pn->c_str();
            } else {
                char buf[16];
                SDL_snprintf(buf, sizeof(buf), "Player #%d", cid.value);
                row.name = buf;
            }

            if (const auto* stats = registry.try_get<PlayerMatchStats>(ent)) {
                row.kills = stats->kills;
                row.deaths = stats->deaths;
            }

            result.rows.push_back(std::move(row));
        });

    if (!result.rows.empty())
        cachedPostMatchResult_ = std::move(result);
}

void Game::quit()
{
    if (window) {
        mouseCaptured = false;
        input_capture::releaseGameplayInputCapture(window);
    }
    if (userSettings) {
        userSettings->mouseSensitivity = mouseSensitivity;
        userSettings->horizontalFovDegrees = horizontalFovDegrees;
    }
    closeChat();
    if (recorder.isRecording())
        recorder.stopRecording();
    perfRecorder_.stop();
    voiceChat_.quit();
    sfxSystem.quit();
    particleSystem.quit();
    hud_.quit();
    if (renderer) {
        for (const AssetEntry& asset : assets_.entries()) {
            if (asset.modelIndex >= 0)
                renderer->setModelScenePass(asset.modelIndex, false);
        }
        renderer->setEntityRenderList({});
        renderer->setWeaponViewmodel({});
        renderer->setPointLights({});
        renderer->setSkinnedFrame({}, {});
        // TODO(renderer-migration): renderer->setParticleSystem(nullptr);
        renderer->setParticleSystem(nullptr);
        renderer->setHudTexture(nullptr);
    }
    if (client) {
        client->onSnapshotApply({});
        client->onRawParticleEvent({});
        client->onMatchStateUpdate({});
        client->onKillEvent({});
        client->onTextChat({});
        client->onRosterEvent({});
        client->onVoiceFrame({});
        client->onShotDebugReport({});
    }
}

void Game::shutdownAfterRenderer()
{
    debugUI.shutdown();
    if (activeGamepad_) {
        SDL_CloseGamepad(activeGamepad_);
        activeGamepad_ = nullptr;
        activeGamepadId_ = 0;
    }
}

void Game::refreshRemotePlayerRenderables()
{
    // Scale + Y offset are driven from the auto-calculated values (and tunable
    // via the Animation Tester panel).
    //
    // The vertical offset places the model's feet at the bottom of the
    // collision AABB: translation.y = -halfExtents.y - meshMinY * scale.
    // This ensures the model tracks the AABB bottom automatically when
    // crouching changes the half-height — no manual offset update needed.
    registry.view<Position, PlayerVisState, InputSnapshot, CollisionShape>().each([&](entt::entity e,
                                                                                      const Position&,
                                                                                      const PlayerVisState& state,
                                                                                      const InputSnapshot& input,
                                                                                      const CollisionShape& shape) {
        if (registry.all_of<LocalPlayer>(e))
            return;

        if (!registry.all_of<AnimatedCharacter>(e))
            attachAnimatedCharacter(e);

        const auto& ac = registry.get<AnimatedCharacter>(e);
        if (!ac.animator)
            return; // rig unavailable — leave entity un-rendered rather than crash.

        auto& rend = registry.get_or_emplace<Renderable>(e);
        // Perf Phase 1B: animated chars are drawn by the renderer's instanced
        // skinned-rig pipeline, NOT via the regular EntityRenderCmd path.
        // Setting modelIndex = -1 keeps Renderable's translation/scale/orientation
        // up-to-date (used to derive the per-instance world transform below)
        // while preventing the entity-render loop from drawing a duplicate.
        rend.modelIndex = -1;
        // Bottom-of-AABB reference: align model feet with the AABB bottom.
        // When gravity is flipped, the player walks on ceilings — the model
        // is rotated 180° around Z so "feet" point upward; translate the model
        // so the (now-inverted) feet align with the AABB top.
        if (state.gravityFlipped)
            rend.translation = glm::vec3(0.0f, shape.halfExtents.y + rigMeshMinY_ * kRigScale_, 0.0f);
        else
            rend.translation = glm::vec3(0.0f, -shape.halfExtents.y - rigMeshMinY_ * kRigScale_, 0.0f);
        rend.scale = glm::vec3(kRigScale_);
        // No importFix quaternion here: rig is loaded with
        // AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS=false which collapses the
        // FBX pre-rotation.  Add a rig-local fix here if the rig ends up
        // facing the wrong axis after a visual check.
        // Gravity-flipped players get an additional 180° roll so they
        // appear upside-down to normally-oriented observers.
        glm::quat orient = glm::angleAxis(input.yaw, glm::vec3{0, 1, 0});
        if (state.gravityFlipped) {
            orient = orient * glm::angleAxis(glm::pi<float>(), glm::vec3{0, 0, 1});
        }
        rend.orientation = orient;
        rend.visible = !state.isDead;
    });
}

void Game::refreshRemoteProjectileRenderables()
{
    registry.view<Position, Projectile, Velocity, CollisionShape>().each([&](entt::entity e,
                                                                             const Position&,
                                                                             const Projectile& projectile,
                                                                             const Velocity& vel,
                                                                             const CollisionShape& /*shape*/) {
        auto& rend = registry.get_or_emplace<Renderable>(e, Renderable{});
        if (projectile.type == WeaponType::HEGrenade) {
            rend.modelIndex = heGrenadeModelIdx_;
            rend.scale = kHEGrenadeModel.renderScale;
        } else if (projectile.type == WeaponType::Sticky) {
            rend.modelIndex = stickyGrenadeModelIdx_;
            rend.scale = kStickyGrenadeModel.renderScale;
        } else if (projectile.type == WeaponType::Molotov) {
            rend.modelIndex = molotovModelIdx_;
            rend.scale = kMolotovModel.renderScale;
        } else {
            rend.modelIndex = rocketProjectileModelIdx_;
            rend.scale = glm::vec3(kRocketProjectile.loadScale);
        }
        rend.visible = rend.modelIndex >= 0;

        if (float len2 = glm::dot(vel.value, vel.value); len2 > 0.001f) {
            glm::vec3 direction = vel.value / std::sqrt(len2);
            rend.orientation = glm::quatLookAt(direction, glm::vec3{0, 1, 0});
        }

        if (!isGrenadeType(projectile.type)) {
            constexpr glm::vec3 modelCenter{0.0f, -1.668982f, 4.484283f};
            rend.translation = rend.orientation * (modelCenter * kRocketProjectile.loadScale);
        }

        if (isGrenadeType(projectile.type) && !projectile.stuck) {
            float spinAngle = projectile.currentLifeTime * 8.0f;
            rend.orientation = rend.orientation * glm::angleAxis(spinAngle, glm::vec3{-1, 0, 0});
        }
    });
}

void Game::refreshRemoteRespawnRenderables()
{
    registry.view<Position, WeaponSpawner, CollisionShape>().each(
        [&](entt::entity e, const Position&, const WeaponSpawner& spawner, const CollisionShape&) {
            auto& rend = registry.get_or_emplace<Renderable>(e, Renderable{});
            const int weaponIndex = static_cast<int>(spawner.type);
            const bool grenadeSpawner = isGrenadeType(spawner.type);

            if (grenadeSpawner) {
                int grenadeIdx = grenadeModelIdx_;
                if (spawner.type == WeaponType::HEGrenade)
                    grenadeIdx = heGrenadeModelIdx_;
                else if (spawner.type == WeaponType::Sticky)
                    grenadeIdx = stickyGrenadeModelIdx_;
                else if (spawner.type == WeaponType::Molotov)
                    grenadeIdx = molotovModelIdx_;
                if (grenadeIdx < 0) {
                    rend.modelIndex = -1;
                    rend.visible = false;
                    return;
                }
                rend.modelIndex = grenadeIdx;
                const WeaponSpawnerModelParams& params = defaultSpawnerModelParams(spawner.type);
                rend.scale = params.scale;

                const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;

                rend.visible = spawner.hasWeapon;

                if (spawner.hasWeapon) {
                    static constexpr float k_twoPi = 6.28318530718f;

                    rend.translation =
                        params.translation +
                        glm::vec3{0.0f, std::sin(t * k_twoPi * params.bobHz) * params.bobAmplitude, 0.0f};
                } else {
                    rend.translation = params.translation;
                }
                rend.orientation = spawnerModelRotation(params, t, spawner.hasWeapon);
                return;
            } else {
                if (weaponIndex < 0 || weaponIndex >= static_cast<int>(kWeaponAssets.size()) ||
                    weaponAssetIds_[static_cast<std::size_t>(weaponIndex)] < 0)
                {
                    rend.modelIndex = -1;
                    rend.visible = false;
                    return;
                }

                const int assetId = weaponAssetIds_[static_cast<std::size_t>(weaponIndex)];
                const AssetEntry& asset = assets_.entry(assetId);
                rend.modelIndex = asset.modelIndex;
            }

            const auto weaponIdx = static_cast<std::size_t>(weaponIndex);
            const WeaponSpawnerModelParams& params = spawnerWeaponParams_[weaponIdx];
            rend.scale = params.scale;

            const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;

            rend.visible = spawner.hasWeapon;

            if (spawner.hasWeapon) {
                static constexpr float k_twoPi = 6.28318530718f;

                rend.translation = params.translation +
                                   glm::vec3{0.0f, std::sin(t * k_twoPi * params.bobHz) * params.bobAmplitude, 0.0f};
            } else {
                rend.translation = params.translation;
            }
            rend.orientation = spawnerModelRotation(params, t, spawner.hasWeapon);
        });
}

void Game::refreshRemotePowerupRenderables()
{
    registry.view<Position, PowerupSpawner, CollisionShape>().each(
        [&](entt::entity e, const Position&, const PowerupSpawner& spawner, const CollisionShape&) {
            auto& rend = registry.get_or_emplace<Renderable>(e, Renderable{});
            const int powerupIndex = rocketProjectileModelIdx_;

            rend.modelIndex = powerupIndex;
            rend.scale = glm::vec3(kRocketProjectile.loadScale);
            rend.visible = spawner.hasPowerup;
        });
}

void Game::refreshRemoteHealthPackRenderables()
{
    registry.view<Position, HealthPackSpawner, CollisionShape>().each(
        [&](entt::entity e, const Position&, const HealthPackSpawner& spawner, const CollisionShape&) {
            auto& rend = registry.get_or_emplace<Renderable>(e, Renderable{});
            const WeaponSpawnerModelParams params{
                .scale = kMedkitModel.renderScale,
                .translation = kMedkitModel.renderTranslation,
                .yawOffset = 0.0f,
                .pitchOffset = 0.0f,
                .rollOffset = 0.0f,
                .spinDegreesPerSecond = 45.0f,
                .bobAmplitude = 6.0f,
                .bobHz = 0.6f,
            };
            const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;

            rend.modelIndex = medkitModelIdx_;
            rend.scale = params.scale;
            if (spawner.hasPack) {
                static constexpr float k_twoPi = 6.28318530718f;
                rend.translation = params.translation +
                                   glm::vec3{0.0f, std::sin(t * k_twoPi * params.bobHz) * params.bobAmplitude, 0.0f};
            } else {
                rend.translation = params.translation;
            }
            rend.orientation = spawnerModelRotation(params, t, spawner.hasPack);
            rend.visible = spawner.hasPack && medkitModelIdx_ >= 0;
        });
}

void Game::refreshDroppedWeaponRenderables()
{
    std::vector<entt::entity> staleDroppedWeaponRenderables;
    registry.view<Renderable, DroppedWeaponRenderableTag>().each([&](entt::entity e, const Renderable&) {
        if (registry.all_of<DroppedWeapon, Position, CollisionShape>(e))
            return;

        staleDroppedWeaponRenderables.push_back(e);
    });
    for (entt::entity e : staleDroppedWeaponRenderables) {
        if (auto* rend = registry.try_get<Renderable>(e)) {
            rend->visible = false;
            rend->modelIndex = -1;
        }
        if (registry.all_of<DroppedWeaponRenderableTag>(e))
            registry.remove<DroppedWeaponRenderableTag>(e);
    }

    registry.view<Position, DroppedWeapon, CollisionShape>().each(
        [&](entt::entity e, const Position&, const DroppedWeapon& dw, const CollisionShape&) {
            auto& rend = registry.get_or_emplace<Renderable>(e, Renderable{});
            registry.emplace_or_replace<DroppedWeaponRenderableTag>(e);
            const int weaponIndex = static_cast<int>(dw.type);
            if (weaponIndex < 0 || weaponIndex >= static_cast<int>(kWeaponAssets.size()) ||
                weaponAssetIds_[static_cast<std::size_t>(weaponIndex)] < 0)
            {
                rend.modelIndex = -1;
                rend.visible = false;
                return;
            }

            const int assetId = weaponAssetIds_[static_cast<std::size_t>(weaponIndex)];
            const AssetEntry& asset = assets_.entry(assetId);

            rend.modelIndex = asset.modelIndex;
            const auto weaponIdx = static_cast<std::size_t>(weaponIndex);
            const WeaponSpawnerModelParams& params = spawnerWeaponParams_[weaponIdx];
            rend.scale = params.scale;

            // Same spin + bob treatment the spawners use, so dropped weapons
            // read as pickups at a glance.
            static constexpr float k_twoPi = 6.28318530718f;

            const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;

            rend.visible = true;
            rend.orientation = spawnerModelRotation(params, t, true);
            rend.translation =
                params.translation + glm::vec3{0.0f, std::sin(t * k_twoPi * params.bobHz) * params.bobAmplitude, 0.0f};
        });
}

void Game::attachAnimatedCharacter(entt::entity e)
{
    if (registry.all_of<AnimatedCharacter>(e))
        return;

    AnimatedCharacter ac;
    ac.animator = std::make_unique<CharacterAnimator>(charRig_, animLibrary_);
    ac.animator->setSkinningBackend(&skinBackend_);

    // Perf Phase 1B: animated chars draw via the renderer's shared skinned-rig
    // path (one VB/IB/textures for all chars, GPU LBS in vertex shader, instanced
    // draw).  No per-entity model clone, no CPU skinning, no per-frame vertex
    // re-upload.  modelIndex stays -1 so the regular EntityRenderCmd path
    // skips animated chars (they're drawn by the skinned pipeline instead).
    ac.modelIndex = -1;

    registry.emplace<AnimatedCharacter>(e, std::move(ac));
}

void Game::onWeaponFired(const WeaponFiredEvent& evt)
{
    if (evt.shooter == entt::null || !registry.valid(evt.shooter))
        return;
    auto* ac = registry.try_get<AnimatedCharacter>(evt.shooter);
    if (ac == nullptr || !ac->animator)
        return;
    // Look up the per-weapon-class kick magnitude. Robust to weapon-type out-of-
    // range (e.g. unrecognized event payloads) — falls through to no impulse.
    const auto typeIdx = static_cast<std::size_t>(evt.type);
    if (typeIdx >= std::size(tpWeaponParams_))
        return;
    ac->animator->applyRecoilImpulse(tpWeaponParams_[typeIdx].recoilKickRad);
}
