/// @file WeaponHoldPose.cpp
/// @brief TOML loader/saver for per-weapon third-person FK hold poses.
///
/// Schema (all angles degrees, [pitch, yaw, roll]; a 2-element [pitch, yaw]
/// entry is still accepted and treated as roll = 0):
/// @code
/// [weapon]
/// offset   = [x, y, z]          # Spine2-local weapon translation
/// rotation = [w, x, y, z]       # Spine2-local weapon rotation quaternion
/// scale    = 39.4               # weapon mesh scale
///
/// [right_arm]
/// bones = [[p,y,r],[p,y,r],[p,y,r],[p,y,r]]   # Shoulder, UpperArm, ForeArm, Hand
/// [right_arm.thumb]
/// joints = [[p,y,r],[p,y,r],[p,y,r],[p,y,r]]
/// [right_arm.index]  ...  # middle, ring, pinky
///
/// [left_arm]  ...               # same shape
/// @endcode

#include "ecs/components/WeaponHoldPose.hpp"

#include <SDL3/SDL_log.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <toml++/toml.hpp>

namespace
{

constexpr std::array<const char*, kGripPoseFingerCount> k_fingerSectionNames{
    "thumb", "index", "middle", "ring", "pinky"};

glm::vec3 readTriple(const toml::array& triples, std::size_t idx)
{
    const auto* node = triples.get(idx);
    if (node == nullptr || !node->is_array())
        return glm::vec3(0.0f);
    const auto& triple = *node->as_array();
    if (triple.size() < 2)
        return glm::vec3(0.0f);
    // roll (3rd element) is optional — a legacy [pitch, yaw] entry loads as roll = 0.
    const double roll = (triple.size() >= 3) ? triple.get(2)->value_or(0.0) : 0.0;
    return glm::vec3{static_cast<float>(triple.get(0)->value_or(0.0)),
                     static_cast<float>(triple.get(1)->value_or(0.0)),
                     static_cast<float>(roll)};
}

void readArm(const toml::table& root, const char* armKey, ArmHoldPose& out)
{
    const auto* armNode = root.get(armKey);
    if (armNode == nullptr || !armNode->is_table())
        return;
    const auto& armTable = *armNode->as_table();

    if (const auto* bonesNode = armTable.get("bones"); bonesNode != nullptr && bonesNode->is_array()) {
        const auto& bones = *bonesNode->as_array();
        for (std::size_t i = 0; i < kArmHoldBoneCount && i < bones.size(); ++i)
            out.boneAngles[i] = readTriple(bones, i);
    }

    for (std::size_t fingerIdx = 0; fingerIdx < kGripPoseFingerCount; ++fingerIdx) {
        const auto* fingerNode = armTable.get(k_fingerSectionNames[fingerIdx]);
        if (fingerNode == nullptr || !fingerNode->is_table())
            continue;
        const auto* jointsNode = fingerNode->as_table()->get("joints");
        if (jointsNode == nullptr || !jointsNode->is_array())
            continue;
        const auto& joints = *jointsNode->as_array();
        for (std::size_t j = 0; j < kGripPoseBonesPerFinger && j < joints.size(); ++j)
            out.fingerAngles[GripPose::index(fingerIdx, j)] = readTriple(joints, j);
    }
}

void writeTriple(std::ofstream& f, const glm::vec3& a, const char* trailComment = nullptr)
{
    char buf[160];
    if (trailComment != nullptr)
        std::snprintf(buf, sizeof(buf), "  [%.2f, %.2f, %.2f],   # %s\n",
                      static_cast<double>(a.x), static_cast<double>(a.y), static_cast<double>(a.z), trailComment);
    else
        std::snprintf(buf, sizeof(buf), "  [%.2f, %.2f, %.2f],\n",
                      static_cast<double>(a.x), static_cast<double>(a.y), static_cast<double>(a.z));
    f << buf;
}

void writeArm(std::ofstream& f, const char* armKey, const ArmHoldPose& arm)
{
    f << "[" << armKey << "]\n";
    f << "# Arm-chain bones, root -> tip: Shoulder, UpperArm, ForeArm, Hand. [pitch, yaw, roll] degrees.\n";
    f << "bones = [\n";
    for (std::size_t i = 0; i < kArmHoldBoneCount; ++i)
        writeTriple(f, arm.boneAngles[i], kArmHoldBoneSuffixes[i]);
    f << "]\n\n";

    for (std::size_t fingerIdx = 0; fingerIdx < kGripPoseFingerCount; ++fingerIdx) {
        f << "[" << armKey << "." << k_fingerSectionNames[fingerIdx] << "]\n";
        f << "joints = [\n";
        for (std::size_t j = 0; j < kGripPoseBonesPerFinger; ++j)
            writeTriple(f, arm.fingerAngles[GripPose::index(fingerIdx, j)]);
        f << "]\n\n";
    }
}

} // namespace

bool loadWeaponHoldPose(const std::string& path, WeaponHoldPose& out)
{
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        SDL_Log("WeaponHoldPose: failed to parse '%s': %s", path.c_str(), err.what());
        return false;
    }

    if (const auto* weaponNode = root.get("weapon"); weaponNode != nullptr && weaponNode->is_table()) {
        const auto& weapon = *weaponNode->as_table();
        if (const auto* offNode = weapon.get("offset"); offNode != nullptr && offNode->is_array()) {
            const auto& off = *offNode->as_array();
            out.spineOffset = glm::vec3{static_cast<float>(off.get(0) ? off.get(0)->value_or(0.0) : 0.0),
                                        static_cast<float>(off.get(1) ? off.get(1)->value_or(0.0) : 0.0),
                                        static_cast<float>(off.get(2) ? off.get(2)->value_or(0.0) : 0.0)};
        }
        if (const auto* rotNode = weapon.get("rotation"); rotNode != nullptr && rotNode->is_array()) {
            const auto& rot = *rotNode->as_array();
            const glm::quat q{static_cast<float>(rot.get(0) ? rot.get(0)->value_or(1.0) : 1.0),
                              static_cast<float>(rot.get(1) ? rot.get(1)->value_or(0.0) : 0.0),
                              static_cast<float>(rot.get(2) ? rot.get(2)->value_or(0.0) : 0.0),
                              static_cast<float>(rot.get(3) ? rot.get(3)->value_or(0.0) : 0.0)};
            out.spineRotation = glm::normalize(q);
        }
        // scale is optional — a file written before scale was persisted keeps
        // the caller's existing value (the compile-time default).
        if (const auto* scaleNode = weapon.get("scale"); scaleNode != nullptr)
            out.scale = static_cast<float>(scaleNode->value_or(static_cast<double>(out.scale)));
    }

    readArm(root, "right_arm", out.rightArm);
    readArm(root, "left_arm", out.leftArm);
    SDL_Log("WeaponHoldPose: loaded '%s'", path.c_str());
    return true;
}

bool saveWeaponHoldPose(const std::string& path, const WeaponHoldPose& pose)
{
    const std::filesystem::path target(path);
    const std::filesystem::path tmp = target.string() + ".tmp";
    {
        std::ofstream f(tmp.string(), std::ios::out | std::ios::trunc);
        if (!f) {
            SDL_Log("WeaponHoldPose: failed to open '%s' for writing", tmp.string().c_str());
            return false;
        }
        f << "# Third-person weapon hold pose authored via the in-game Weapon Hold tweaker.\n";
        f << "# The weapon is a rigid child of Spine2; arms are pure FK (per-bone pitch+yaw, degrees).\n\n";
        f << "[weapon]\n";
        char buf[160];
        std::snprintf(buf, sizeof(buf), "offset   = [%.3f, %.3f, %.3f]\n",
                      static_cast<double>(pose.spineOffset.x), static_cast<double>(pose.spineOffset.y),
                      static_cast<double>(pose.spineOffset.z));
        f << buf;
        std::snprintf(buf, sizeof(buf), "rotation = [%.5f, %.5f, %.5f, %.5f]\n",
                      static_cast<double>(pose.spineRotation.w), static_cast<double>(pose.spineRotation.x),
                      static_cast<double>(pose.spineRotation.y), static_cast<double>(pose.spineRotation.z));
        f << buf;
        std::snprintf(buf, sizeof(buf), "scale    = %.3f\n\n", static_cast<double>(pose.scale));
        f << buf;
        writeArm(f, "right_arm", pose.rightArm);
        writeArm(f, "left_arm", pose.leftArm);
    }

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        SDL_Log("WeaponHoldPose: rename '%s' -> '%s' failed: %s",
                tmp.string().c_str(), target.string().c_str(), ec.message().c_str());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    SDL_Log("WeaponHoldPose: saved '%s'", path.c_str());
    return true;
}
