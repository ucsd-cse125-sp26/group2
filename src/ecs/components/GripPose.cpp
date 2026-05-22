/// @file GripPose.cpp
/// @brief TOML loader for per-weapon hand grip poses.

#include "ecs/components/GripPose.hpp"

#include <SDL3/SDL_log.h>
#include <array>
#include <glm/gtc/constants.hpp>
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
        const auto* tripleNode = jointsArray.get(j);
        if (tripleNode == nullptr || !tripleNode->is_array())
            return false;
        const auto& triple = *tripleNode->as_array();
        if (triple.size() < 3)
            return false;

        const double xDeg = triple.get(0)->value_or(0.0);
        const double yDeg = triple.get(1)->value_or(0.0);
        const double zDeg = triple.get(2)->value_or(0.0);
        const float xRad = glm::radians(static_cast<float>(xDeg));
        const float yRad = glm::radians(static_cast<float>(yDeg));
        const float zRad = glm::radians(static_cast<float>(zDeg));

        // XYZ intrinsic Euler → quat: qx * qy * qz applied in local frame.
        const glm::quat qx = glm::angleAxis(xRad, glm::vec3{1.0f, 0.0f, 0.0f});
        const glm::quat qy = glm::angleAxis(yRad, glm::vec3{0.0f, 1.0f, 0.0f});
        const glm::quat qz = glm::angleAxis(zRad, glm::vec3{0.0f, 0.0f, 1.0f});
        out.jointRotations[GripPose::index(fingerIdx, j)] = glm::normalize(qx * qy * qz);
    }
    return true;
}

bool readHandTable(const toml::table& root, const char* handKey, GripPose& out)
{
    const auto* handNode = root.get(handKey);
    if (handNode == nullptr || !handNode->is_table())
        return false;
    const auto& handTable = *handNode->as_table();
    // Initialize all joints to identity so partial files don't carry garbage.
    for (auto& q : out.jointRotations)
        q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bool anyFinger = false;
    for (std::size_t fingerIdx = 0; fingerIdx < kGripPoseFingerCount; ++fingerIdx) {
        if (readFingerJoints(handTable, k_fingerSectionNames[fingerIdx], out, fingerIdx))
            anyFinger = true;
    }
    return anyFinger;
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
