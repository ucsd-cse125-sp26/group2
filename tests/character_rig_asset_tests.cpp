#include "client/animation/CharacterRig.hpp"
#include "client/animation/FbxImportUtils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/skeleton.h>

namespace
{
void printBoundsAndJoints(const char* relativePath, CharacterRig& rig)
{
    float minY = 0.0f;
    float maxY = 0.0f;
    rig.verticalBounds(minY, maxY);
    std::cout << relativePath << " boundsY=[" << minY << ", " << maxY << "] height=" << (maxY - minY) << '\n';
    rig.verticalBoundsForJointPrefix("mixamorig:", minY, maxY);
    std::cout << relativePath << " mixamoBoundsY=[" << minY << ", " << maxY << "] height=" << (maxY - minY)
              << '\n';

    const auto& meshes = rig.meshes();
    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        std::array<float, 3> mins{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        std::array<float, 3> maxs{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
        };
        for (const auto& vert : meshes[meshIndex].baseVertices) {
            mins[0] = std::min(mins[0], vert.position.x);
            mins[1] = std::min(mins[1], vert.position.y);
            mins[2] = std::min(mins[2], vert.position.z);
            maxs[0] = std::max(maxs[0], vert.position.x);
            maxs[1] = std::max(maxs[1], vert.position.y);
            maxs[2] = std::max(maxs[2], vert.position.z);
        }
        std::cout << "  mesh " << meshIndex << " xyzMin=[" << mins[0] << ", " << mins[1] << ", " << mins[2]
                  << "] xyzMax=[" << maxs[0] << ", " << maxs[1] << ", " << maxs[2]
                  << "] verts=" << meshes[meshIndex].baseVertices.size() << '\n';
    }

    const auto& jointMap = rig.jointMap();
    for (const char* name :
         {"mixamorig:Spine2", "APEX_wraith_gameplay_arm_rig", "jx_c_delta", "def_l_shoulder", "def_r_shoulder"})
    {
        auto it = jointMap.find(name);
        std::cout << "  joint " << name << ": " << (it == jointMap.end() ? -1 : it->second) << '\n';
    }
}

bool requireRigLoads(const char* relativePath,
                     const glm::quat& orientationFix,
                     const char* label,
                     float minHeight,
                     float maxHeight,
                     bool requireUprightRestPose,
                     std::initializer_list<const char*> extraRequiredJoints = {})
{
    const std::filesystem::path path = std::filesystem::current_path() / relativePath;
    CharacterRig rig;
    if (!rig.loadFromFBX(path.string(), orientationFix, true)) {
        std::cerr << relativePath << " failed to load\n";
        return false;
    }
    if (rig.numJoints() <= 0) {
        std::cerr << relativePath << " has no runtime joints\n";
        return false;
    }
    if (rig.meshes().empty()) {
        std::cerr << relativePath << " has no skinned meshes\n";
        return false;
    }
    if (!rig.jointMap().contains("mixamorig:Hips") || !rig.jointMap().contains("mixamorig:Spine2")) {
        std::cerr << relativePath << " is missing required Mixamo body joints\n";
        return false;
    }
    for (const char* jointName : extraRequiredJoints) {
        if (!rig.jointMap().contains(jointName)) {
            std::cerr << relativePath << " is missing required joint " << jointName << '\n';
            return false;
        }
    }
    float boundsMinY = 0.0f;
    float boundsMaxY = 0.0f;
    rig.verticalBounds(boundsMinY, boundsMaxY);
    const float height = boundsMaxY - boundsMinY;
    if (height < minHeight || height > maxHeight) {
        std::cerr << relativePath << " imported with unexpected height " << height << " for " << label << '\n';
        return false;
    }

    const auto headIt = rig.jointMap().find("mixamorig:Head");
    const auto hipsIt = rig.jointMap().find("mixamorig:Hips");
    if (requireUprightRestPose && headIt != rig.jointMap().end() && hipsIt != rig.jointMap().end()) {
        std::vector<ozz::math::Float4x4> models(static_cast<size_t>(rig.numJoints()));
        ozz::animation::LocalToModelJob l2m;
        l2m.skeleton = rig.skeleton();
        l2m.input = rig.skeleton()->joint_rest_poses();
        l2m.output = ozz::make_span(models);
        if (!l2m.Run()) {
            std::cerr << relativePath << " failed rest-pose local-to-model\n";
            return false;
        }
        const glm::vec3 hips = glm::vec3(anim_utils::ozzToGlm(models[static_cast<size_t>(hipsIt->second)])[3]);
        const glm::vec3 head = glm::vec3(anim_utils::ozzToGlm(models[static_cast<size_t>(headIt->second)])[3]);
        if (head.y <= hips.y + 0.5f) {
            std::cerr << relativePath << " has invalid upright rest pose for " << label << ": hipsY=" << hips.y
                      << " headY=" << head.y << '\n';
            return false;
        }
    }

    std::cout << "orientation=" << label << '\n';
    printBoundsAndJoints(relativePath, rig);
    return true;
}

bool requireApexRigScalesFromMixamoBody(const glm::quat& orientationFix)
{
    CharacterRig originalRig;
    CharacterRig apexRig;
    const std::filesystem::path originalPath =
        std::filesystem::current_path() / "assets/animations/character_rigged_new.glb";
    const std::filesystem::path apexPath =
        std::filesystem::current_path() / "assets/animations/character_rigged_apex_hands.glb";
    if (!originalRig.loadFromFBX(originalPath.string(), orientationFix, true) ||
        !apexRig.loadFromFBX(apexPath.string(), orientationFix, true))
    {
        std::cerr << "failed to load rigs for Mixamo-only bounds comparison\n";
        return false;
    }

    float originalMinY = 0.0f;
    float originalMaxY = 1.0f;
    originalRig.verticalBoundsForJointPrefix("mixamorig:", originalMinY, originalMaxY);

    float apexMinY = 0.0f;
    float apexMaxY = 1.0f;
    apexRig.verticalBoundsForJointPrefix("mixamorig:", apexMinY, apexMaxY);

    const float originalHeight = originalMaxY - originalMinY;
    const float apexBodyHeight = apexMaxY - apexMinY;
    if (std::abs(originalHeight - apexBodyHeight) > 0.35f) {
        std::cerr << "Apex stitched rig Mixamo body height drifted too far from original body: original="
                  << originalHeight << " apexBody=" << apexBodyHeight << '\n';
        return false;
    }
    return true;
}

bool restJointModelPosition(const CharacterRig& rig, const char* jointName, glm::vec3& out)
{
    const auto it = rig.jointMap().find(jointName);
    if (it == rig.jointMap().end())
        return false;

    std::vector<ozz::math::Float4x4> models(static_cast<size_t>(rig.numJoints()));
    ozz::animation::LocalToModelJob l2m;
    l2m.skeleton = rig.skeleton();
    l2m.input = rig.skeleton()->joint_rest_poses();
    l2m.output = ozz::make_span(models);
    if (!l2m.Run())
        return false;

    out = glm::vec3(anim_utils::ozzToGlm(models[static_cast<size_t>(it->second)])[3]);
    return true;
}

bool requireApexPropSocketNearRightHand(const glm::quat& orientationFix)
{
    CharacterRig apexRig;
    const std::filesystem::path apexPath =
        std::filesystem::current_path() / "assets/animations/character_rigged_apex_hands.glb";
    if (!apexRig.loadFromFBX(apexPath.string(), orientationFix, true)) {
        std::cerr << "failed to load Apex stitched rig for prop socket check\n";
        return false;
    }

    glm::vec3 propGun(0.0f);
    glm::vec3 rightHand(0.0f);
    if (!restJointModelPosition(apexRig, "ja_c_propGun", propGun) ||
        !restJointModelPosition(apexRig, "ja_r_propHand", rightHand))
    {
        std::cerr << "Apex stitched rig missing prop socket joints for rest-pose placement check\n";
        return false;
    }

    const float distance = glm::length(propGun - rightHand);
    if (distance > 0.055f) {
        std::cerr << "Apex prop-gun socket is not driven into the weapon hand rest pose; distance to right prop hand="
                  << distance << '\n';
        return false;
    }
    return true;
}

bool requireR301UsesApexSocketScale()
{
    const std::filesystem::path configPath =
        std::filesystem::current_path().parent_path().parent_path() / "src/ecs/components/ViewmodelConfig.hpp";
    std::ifstream input(configPath);
    if (!input) {
        std::cerr << "failed to open " << configPath << '\n';
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();
    const size_t thirdPersonParams = text.find("static const std::array<ThirdPersonWeaponParams");
    if (thirdPersonParams == std::string::npos) {
        std::cerr << "failed to locate ThirdPersonWeaponParams table in " << configPath << '\n';
        return false;
    }
    const size_t rifleComment = text.find("// Rifle", thirdPersonParams);
    const size_t rocketComment = text.find("// Rocket", rifleComment == std::string::npos ? 0 : rifleComment);
    if (rifleComment == std::string::npos || rocketComment == std::string::npos || rocketComment <= rifleComment) {
        std::cerr << "failed to locate Rifle third-person weapon params in " << configPath << '\n';
        return false;
    }
    const std::string rifleBlock = text.substr(rifleComment, rocketComment - rifleComment);
    if (rifleBlock.find(".scale = 1.0f") == std::string::npos) {
        std::cerr << "R301 third-person scale must stay at native game scale 1.0\n";
        return false;
    }
    return true;
}
} // namespace

int main()
{
    const glm::quat legacyOrientationFix = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    bool ok = true;
    ok = requireRigLoads(
             "assets/animations/character_rigged_new.glb", legacyOrientationFix, "legacy_x_minus_90", 2.5f, 3.5f, false) &&
         ok;
    ok = requireRigLoads("assets/animations/character_rigged_apex_hands.glb",
                         legacyOrientationFix,
                         "legacy_x_minus_90",
                         2.4f,
                         3.2f,
                         true,
                         {"jx_c_delta", "def_l_clav", "def_r_clav", "def_l_shoulder", "def_r_shoulder",
                          "ja_c_propGun"}) &&
         ok;
    ok = requireApexRigScalesFromMixamoBody(legacyOrientationFix) && ok;
    ok = requireApexPropSocketNearRightHand(legacyOrientationFix) && ok;
    ok = requireR301UsesApexSocketScale() && ok;
    return ok ? 0 : 1;
}
