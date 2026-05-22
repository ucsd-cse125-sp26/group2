/// @file GripPose.cpp
/// @brief TOML loader for per-weapon hand grip poses.

#include "ecs/components/GripPose.hpp"

#include <SDL3/SDL_log.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <toml++/toml.hpp>

namespace
{

constexpr std::array<const char*, kGripPoseFingerCount> k_fingerSectionNames{
    "thumb", "index", "middle", "ring", "pinky"};

glm::quat eulerXYZToQuat(const glm::vec3& degrees)
{
    const float xRad = glm::radians(degrees.x);
    const float yRad = glm::radians(degrees.y);
    const float zRad = glm::radians(degrees.z);
    const glm::quat qx = glm::angleAxis(xRad, glm::vec3{1.0f, 0.0f, 0.0f});
    const glm::quat qy = glm::angleAxis(yRad, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat qz = glm::angleAxis(zRad, glm::vec3{0.0f, 0.0f, 1.0f});
    return glm::normalize(qx * qy * qz);
}

glm::vec3 quatToEulerXYZ(const glm::quat& q)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    // glm::extractEulerAngleXYZ expects the matrix to encode `qx*qy*qz` in
    // intrinsic order — matches our loader's build order, so the round-trip
    // is exact away from gimbal lock.
    glm::extractEulerAngleXYZ(glm::mat4_cast(glm::normalize(q)), x, y, z);
    return glm::vec3{glm::degrees(x), glm::degrees(y), glm::degrees(z)};
}

bool readFingerJoints(const toml::table& handTable,
                      const char* fingerName,
                      GripPose& out,
                      glm::vec3* outEulerBase,
                      std::size_t fingerIdx)
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
        const glm::vec3 eulerDeg(static_cast<float>(xDeg), static_cast<float>(yDeg), static_cast<float>(zDeg));
        const std::size_t flatIdx = GripPose::index(fingerIdx, j);
        out.jointRotations[flatIdx] = eulerXYZToQuat(eulerDeg);
        if (outEulerBase != nullptr)
            outEulerBase[flatIdx] = eulerDeg;
    }
    return true;
}

bool readHandTable(const toml::table& root, const char* handKey, GripPose& out, glm::vec3* outEulerBase)
{
    const auto* handNode = root.get(handKey);
    if (handNode == nullptr || !handNode->is_table())
        return false;
    const auto& handTable = *handNode->as_table();
    // Initialize all joints to identity so partial files don't carry garbage.
    for (auto& q : out.jointRotations)
        q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (outEulerBase != nullptr) {
        for (std::size_t i = 0; i < kGripPoseJointCount; ++i)
            outEulerBase[i] = glm::vec3(0.0f);
    }
    bool anyFinger = false;
    for (std::size_t fingerIdx = 0; fingerIdx < kGripPoseFingerCount; ++fingerIdx) {
        if (readFingerJoints(handTable, k_fingerSectionNames[fingerIdx], out, outEulerBase, fingerIdx))
            anyFinger = true;
    }
    return anyFinger;
}

bool loadWeaponGripPoseImpl(const std::string& path, WeaponGripPose& out, WeaponGripPoseEuler* outEulers)
{
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        SDL_Log("GripPose: failed to parse '%s': %s", path.c_str(), err.what());
        return false;
    }

    glm::vec3* rightEulerPtr = outEulers != nullptr ? outEulers->rightHand.data() : nullptr;
    glm::vec3* leftEulerPtr = outEulers != nullptr ? outEulers->leftHand.data() : nullptr;
    out.rightHandValid = readHandTable(root, "right_hand", out.rightHand, rightEulerPtr);
    out.leftHandValid = readHandTable(root, "left_hand", out.leftHand, leftEulerPtr);
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

void writeFingerSection(std::ofstream& f, const char* hand, const char* finger, const glm::vec3* joints)
{
    f << "[" << hand << "." << finger << "]\n";
    f << "joints = [\n";
    for (std::size_t j = 0; j < kGripPoseBonesPerFinger; ++j) {
        char buf[128];
        std::snprintf(buf,
                      sizeof(buf),
                      "  [%.2f, %.2f, %.2f],\n",
                      static_cast<double>(joints[j].x),
                      static_cast<double>(joints[j].y),
                      static_cast<double>(joints[j].z));
        f << buf;
    }
    f << "]\n\n";
}

void writeHandSection(std::ofstream& f, const char* handKey, const std::array<glm::vec3, kGripPoseJointCount>& eulers)
{
    for (std::size_t fingerIdx = 0; fingerIdx < kGripPoseFingerCount; ++fingerIdx) {
        const glm::vec3 jointEulers[kGripPoseBonesPerFinger] = {
            eulers[GripPose::index(fingerIdx, 0)],
            eulers[GripPose::index(fingerIdx, 1)],
            eulers[GripPose::index(fingerIdx, 2)],
            eulers[GripPose::index(fingerIdx, 3)],
        };
        writeFingerSection(f, handKey, k_fingerSectionNames[fingerIdx], jointEulers);
    }
}

} // namespace

bool loadWeaponGripPose(const std::string& path, WeaponGripPose& out)
{
    return loadWeaponGripPoseImpl(path, out, nullptr);
}

bool loadWeaponGripPoseWithEulers(const std::string& path, WeaponGripPose& out, WeaponGripPoseEuler& outEulers)
{
    // Zero-out the Euler arrays so a hand that's missing from disk lands at
    // identity (0,0,0) rather than carrying stale data from a previous load.
    for (auto& v : outEulers.rightHand)
        v = glm::vec3(0.0f);
    for (auto& v : outEulers.leftHand)
        v = glm::vec3(0.0f);
    return loadWeaponGripPoseImpl(path, out, &outEulers);
}

void gripPoseQuatsFromEulers(const WeaponGripPoseEuler& eulers, WeaponGripPose& out)
{
    for (std::size_t i = 0; i < kGripPoseJointCount; ++i) {
        out.rightHand.jointRotations[i] = eulerXYZToQuat(eulers.rightHand[i]);
        out.leftHand.jointRotations[i] = eulerXYZToQuat(eulers.leftHand[i]);
    }
}

void gripPoseEulersFromQuats(const WeaponGripPose& pose, WeaponGripPoseEuler& outEulers)
{
    for (std::size_t i = 0; i < kGripPoseJointCount; ++i) {
        outEulers.rightHand[i] = quatToEulerXYZ(pose.rightHand.jointRotations[i]);
        outEulers.leftHand[i] = quatToEulerXYZ(pose.leftHand.jointRotations[i]);
    }
}

bool saveWeaponGripPoseToml(const std::string& path,
                            const WeaponGripPoseEuler& eulers,
                            bool rightHandValid,
                            bool leftHandValid)
{
    // Write to a sibling temp file then atomically rename, so a concurrent
    // hot-reload poll can't catch a partially-written TOML.
    const std::filesystem::path target(path);
    const std::filesystem::path tmp = target.string() + ".tmp";

    {
        std::ofstream f(tmp.string(), std::ios::out | std::ios::trunc);
        if (!f) {
            SDL_Log("GripPose: failed to open '%s' for writing", tmp.string().c_str());
            return false;
        }
        f << "# Grip pose authored via in-game tweaker — local-space finger rotations (Euler degrees XYZ).\n";
        f << "# Coordinate convention (Mixamo finger rig):\n";
        f << "#   X = along the bone (toward the tip)\n";
        f << "#   Y = spread axis (sideways between fingers)\n";
        f << "#   Z = curl axis (positive = curl into a fist)\n\n";
        if (rightHandValid)
            writeHandSection(f, "right_hand", eulers.rightHand);
        if (leftHandValid)
            writeHandSection(f, "left_hand", eulers.leftHand);
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
