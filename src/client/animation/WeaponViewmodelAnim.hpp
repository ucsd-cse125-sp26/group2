/// @file WeaponViewmodelAnim.hpp
/// @brief Self-contained animated first-person weapon viewmodel.
///
/// Loads a skinned weapon GLB (skeleton + skinned meshes + multiple baked
/// animation clips, e.g. the Apex R-301 `apex_r301.glb`) and plays one clip at
/// a time (idle/draw/fire/reload/holster), producing a per-frame bone palette
/// (skinning matrices) for the skinned renderer.
///
/// Decoupled from the locomotion `CharacterAnimator` (which is Mixamo/state-
/// machine specific).  Reuses `CharacterRig` for the Assimp→ozz skeleton +
/// bind-pose mesh load, and ozz SamplingJob/LocalToModelJob for posing.

#pragma once

#include "CharacterRig.hpp"
#include "renderer-new/RendererTypes.hpp" // RigMeshSource

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

class WeaponViewmodelAnim
{
public:
    WeaponViewmodelAnim();
    ~WeaponViewmodelAnim();
    WeaponViewmodelAnim(const WeaponViewmodelAnim&) = delete;
    WeaponViewmodelAnim& operator=(const WeaponViewmodelAnim&) = delete;

    /// @brief Load the skinned weapon GLB: rig (skeleton + meshes) + ALL baked
    /// animation clips contained in the file (keyed by their glTF animation name).
    bool load(const std::string& glbPath, bool flipUVs = false);

    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] int numJoints() const;

    /// @brief Build the renderer rig-source list (bind verts + bone influences + indices).
    [[nodiscard]] std::vector<RigMeshSource> buildRigSources() const;

    /// @brief True if a clip with this name was loaded from the GLB.
    [[nodiscard]] bool hasClip(const std::string& name) const;

    /// @brief Duration (seconds) of a clip, or 0 if absent.
    [[nodiscard]] float clipDuration(const std::string& name) const;

    /// @brief Start a clip from t=0.  `loop` keeps it cycling; otherwise it
    /// clamps at the end and `clipFinished()` returns true.  `speed` scales
    /// playback (e.g. clipDuration/reloadTime to fit a clip to a gameplay timer).
    void playClip(const std::string& name, bool loop, float speed = 1.0f);

    /// @brief Stop any active clip and hold the skeleton rest pose.
    void playRestPose();

    /// @brief Currently-playing clip name ("" if rest pose).
    [[nodiscard]] const std::string& currentClip() const;

    /// @brief Kick the bolt/charging-handle bone for one shot (overlaid on the
    /// current pose by update()).  Call once per shot.
    void triggerFire();

    /// @brief Advance the active clip by dt and recompute the bone palette.
    void update(float dtSec);

    /// @brief True once a non-looping clip has reached its end.
    [[nodiscard]] bool clipFinished() const;

    /// @brief Per-joint skinning matrices for this frame (size == numJoints()).
    [[nodiscard]] const std::vector<glm::mat4>& skinMatrices() const;

    /// @brief Current model-space position of a named bone (e.g. "def_c_bolt"
    /// for the chamber/ejection origin).  Zero if absent.
    [[nodiscard]] glm::vec3 boneModelPos(const std::string& name) const;

    /// @brief Full model-space matrix of a named bone for the current pose.
    /// Returns false (and leaves `out` unchanged) if the bone is absent. Used to
    /// read the weapon's `ja_c_propGun` bind transform for third-person mounting.
    [[nodiscard]] bool boneModelMatrix(const std::string& name, glm::mat4& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
