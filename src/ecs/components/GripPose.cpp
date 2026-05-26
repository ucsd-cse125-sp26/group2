/// @file GripPose.cpp
/// @brief TOML loader/saver for per-weapon hand grip poses (pitch+yaw schema).

#include "ecs/components/GripPose.hpp"

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

bool readFingerJoints(const toml::table& handTable, const char* fingerName, GripPose& out, std::size_t fingerIdx)
{
    const auto* fingerNode = handTable.get(fingerName);
    if (fingerNode == nullptr || !fingerNode->is_table())
        return false;
    const auto* jointsNode = fingerNode->as_table()->get("joints");
    if (jointsNode == nullptr || !jointsNode->is_array())
        return false;
    const auto& jointsArray = *jointsNode->as_array();
    for (std::size_t j = 0; j < kGripPoseBonesPerFinger; ++j) {
        if (j >= jointsArray.size())
            return false;
        const auto* pairNode = jointsArray.get(j);
        if (pairNode == nullptr || !pairNode->is_array())
            return false;
        const auto& pair = *pairNode->as_array();
        // Accept either [pitch, yaw] (the modern schema) or [pitch, yaw, roll]
        // (legacy data where roll exists but is discarded). Anything shorter
        // is malformed — caller treats `false` as "leave hand un-authored".
        if (pair.size() < 2)
            return false;

        const double pitchDeg = pair.get(0)->value_or(0.0);
        const double yawDeg = pair.get(1)->value_or(0.0);
        out.jointAngles[GripPose::index(fingerIdx, j)] =
            glm::vec2{static_cast<float>(pitchDeg), static_cast<float>(yawDeg)};
    }
    return true;
}

bool readHandTable(const toml::table& root, const char* handKey, GripPose& out)
{
    const auto* handNode = root.get(handKey);
    if (handNode == nullptr || !handNode->is_table())
        return false;
    const auto& handTable = *handNode->as_table();
    // Initialize all joints to zero so partial files don't carry garbage.
    for (auto& a : out.jointAngles)
        a = glm::vec2(0.0f);
    bool anyFinger = false;
    for (std::size_t fingerIdx = 0; fingerIdx < kGripPoseFingerCount; ++fingerIdx) {
        if (readFingerJoints(handTable, k_fingerSectionNames[fingerIdx], out, fingerIdx))
            anyFinger = true;
    }
    return anyFinger;
}

void writeFingerSection(std::ofstream& f,
                        const char* hand,
                        const char* finger,
                        const std::array<glm::vec2, kGripPoseJointCount>& angles,
                        std::size_t fingerIdx)
{
    f << "[" << hand << "." << finger << "]\n";
    f << "joints = [\n";
    for (std::size_t j = 0; j < kGripPoseBonesPerFinger; ++j) {
        const glm::vec2& a = angles[GripPose::index(fingerIdx, j)];
        char buf[96];
        std::snprintf(buf,
                      sizeof(buf),
                      "  [%.2f, %.2f],\n",
                      static_cast<double>(a.x),
                      static_cast<double>(a.y));
        f << buf;
    }
    f << "]\n\n";
}

void writeHandSection(std::ofstream& f,
                      const char* handKey,
                      const std::array<glm::vec2, kGripPoseJointCount>& angles)
{
    for (std::size_t fingerIdx = 0; fingerIdx < kGripPoseFingerCount; ++fingerIdx)
        writeFingerSection(f, handKey, k_fingerSectionNames[fingerIdx], angles, fingerIdx);
}

} // namespace

bool loadWeaponGripPose(const std::string& path, WeaponGripPose& out)
{
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        SDL_Log("GripPose: failed to parse '%s': %s", path.c_str(), err.what());
        return false;
    }

    out.rightHandValid = readHandTable(root, "right_hand", out.rightHand);
    out.leftHandValid = readHandTable(root, "left_hand", out.leftHand);
    if (!out.rightHandValid && !out.leftHandValid) {
        SDL_Log("GripPose: '%s' parsed but no hands defined", path.c_str());
        return false;
    }
    SDL_Log("GripPose: loaded '%s' (right=%d, left=%d)",
            path.c_str(),
            out.rightHandValid ? 1 : 0,
            out.leftHandValid ? 1 : 0);
    return true;
}

bool saveWeaponGripPoseToml(const std::string& path,
                            const WeaponGripPose& pose,
                            bool rightHandValid,
                            bool leftHandValid)
{
    // Write to a sibling temp file then atomically rename so a concurrent
    // hot-reload poll can't catch a partially-written TOML.
    const std::filesystem::path target(path);
    const std::filesystem::path tmp = target.string() + ".tmp";

    {
        std::ofstream f(tmp.string(), std::ios::out | std::ios::trunc);
        if (!f) {
            SDL_Log("GripPose: failed to open '%s' for writing", tmp.string().c_str());
            return false;
        }
        f << "# Grip pose authored via in-game tweaker — per-joint pitch & yaw (degrees).\n";
        f << "# Convention (Mixamo finger rig, parent-local frame):\n";
        f << "#   pitch = curl around Z axis (positive = curl into a fist)\n";
        f << "#   yaw   = splay around Y axis (positive = spread outward)\n";
        f << "# No roll DOF — finger joints cannot twist about their own length.\n\n";
        if (rightHandValid)
            writeHandSection(f, "right_hand", pose.rightHand.jointAngles);
        if (leftHandValid)
            writeHandSection(f, "left_hand", pose.leftHand.jointAngles);
    }

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        SDL_Log("GripPose: rename '%s' -> '%s' failed: %s",
                tmp.string().c_str(),
                target.string().c_str(),
                ec.message().c_str());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    SDL_Log("GripPose: saved '%s'", path.c_str());
    return true;
}
