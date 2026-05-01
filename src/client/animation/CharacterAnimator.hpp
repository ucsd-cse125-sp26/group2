/// @file CharacterAnimator.hpp
/// @brief Per-entity animator — state machine, sampling, blending, CPU skinning.

#pragma once

#include "AnimationLibrary.hpp"
#include "CharacterRig.hpp"
#include "SkinningBackend.hpp"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

/// @brief Driving inputs for the animation state machine.
///
/// Read from the ECS each frame.  `moveMode` is the integer value of
/// `MoveMode` from PlayerVisState (avoids a header dependency here).
struct AnimationInputs
{
    glm::vec3 velocityWorld{0.0f}; ///< World-space velocity (u/s).
    float yawRad = 0.0f;           ///< Player yaw (radians).
    float pitchRad = 0.0f;         ///< Player pitch (radians, positive = looking down).
    bool grounded = false;         ///< Touching the ground this tick.
    bool sprinting = false;        ///< Sprint key currently held.
    bool crouching = false;        ///< Crouch currently held (phase 1: note-only).
    int moveMode = 0;              ///< MoveMode value: 0=OnFoot, 1=Sliding, 2=WallRunning, 3=Climbing, 4=LedgeGrabbing.
    int wallRunSide = 0;           ///< WallSide value: 0=None, 1=Left, 2=Right.
};

/// Number of sampler slots available for the per-frame blend.
///
///  [0] locomotion primary    (Idle / Walk / Run / RunBackward)
///  [1] locomotion secondary  (for 1-D speed band blend)
///  [2] locomotion strafe     (StrafeLeft/Right, WalkStrafe/RunStrafe)
///  [3] override              (Slide / WallRun / Jump / debug clip)
///  [4] reserved              (future: additive upper-body layer)
static constexpr size_t kNumSamplerSlots = 5;

/// @brief One active sampler slot contributing to the per-frame blend.
struct ClipSampler
{
    ClipId id = ClipId::Idle;   ///< Which clip.
    float timeRatio = 0.0f;     ///< Normalised playback time ∈ [0, 1].
    float playbackSpeed = 1.0f; ///< Multiplier applied to clip duration when advancing.
    float weight = 0.0f;        ///< Blend weight; slots with weight 0 are skipped.
    bool active = false;        ///< False = slot unused this frame.
};

/// @brief Per-entity animator.
///
/// Runs a small state machine that chooses which clips play, samples + blends
/// them via ozz, converts to model-space matrices, and hands off to the
/// skinning backend to produce deformed vertices.
///
/// Needs non-owning references to the shared rig + clip library + skinning
/// backend, all of which must outlive the animator.
class CharacterAnimator
{
public:
    CharacterAnimator(const CharacterRig& rig, const AnimationLibrary& library);
    ~CharacterAnimator();
    CharacterAnimator(const CharacterAnimator&) = delete;
    CharacterAnimator& operator=(const CharacterAnimator&) = delete;
    CharacterAnimator(CharacterAnimator&&) noexcept = delete;
    CharacterAnimator& operator=(CharacterAnimator&&) noexcept = delete;

    /// @brief Set the skinning backend (non-owning).  Must outlive the animator.
    /// Phase-1: use `CpuLbsSkinningBackend`.  Phase-2: `GpuSkinningBackend`
    /// will slot in here unchanged.
    void setSkinningBackend(const ISkinningBackend* backend);

    /// @brief Advance the state machine, sample + blend, and compute skin matrices.
    /// @param inputs  Current driving inputs (velocity, yaw, movement mode, …).
    /// @param dt      Frame time in seconds.
    void update(const AnimationInputs& inputs, float dt);

    /// @brief Apply CPU skinning to every rig mesh, producing animated vertices.
    /// @param out  One output vector per rig mesh; internally resized.
    void computeSkinnedVertices(std::vector<std::vector<ModelVertex>>& out) const;

    /// @brief Force a single clip to play, bypassing the graph.
    /// @param id   `ClipId::_Count` to clear the override.
    /// @param loop Looping mode (reserved; current implementation always loops).
    void setDebugOverride(ClipId id, bool loop = true);

    /// @brief Current debug override clip (`_Count` = no override).
    [[nodiscard]] ClipId debugOverride() const noexcept;

    /// @brief Snapshot of the current sampler slots (for UI inspection).
    [[nodiscard]] const std::array<ClipSampler, kNumSamplerSlots>& samplers() const noexcept;

    /// @brief Playback-speed multiplier applied in debug-override mode.
    void setDebugPlaybackSpeed(float mul) noexcept;

    /// @brief Number of joints in the underlying rig.
    [[nodiscard]] int numJoints() const noexcept;

    /// @brief Model-space joint matrices with all procedural transforms applied
    ///        (head pitch, wallrun mirror) but WITHOUT inverse-bind-matrix multiplication.
    ///
    /// These are the matrices needed for hitbox capsule placement — each matrix
    /// transforms from bone-local space to the rig's model space.  Apply the
    /// entity's world transform (position + yaw + scale) on top to get world space.
    ///
    /// Valid after a call to update().  Size = numJoints().
    [[nodiscard]] const std::vector<glm::mat4>& jointModelMatrices() const noexcept;

    /// @brief Per-joint LBS skin matrices: `procedural * modelMat * inverseBind`.
    ///        Identical to what `computeSkinnedVertices()` uses internally.
    ///        Used by GPU skinning (perf Phase 1B) — flatten across all visible
    ///        characters into a single palette SSBO and consume by the vertex
    ///        shader.  Valid after a call to update().  Size = numJoints().
    [[nodiscard]] const std::vector<glm::mat4>& skinMatrices() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
