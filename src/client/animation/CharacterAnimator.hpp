/// @file CharacterAnimator.hpp
/// @brief Per-entity animator — state machine, sampling, blending, CPU skinning.

#pragma once

#include "AnimationLibrary.hpp"
#include "CharacterRig.hpp"
#include "SkinningBackend.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/GripPose.hpp"
#include "ecs/components/WeaponHoldPose.hpp"

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

/// @brief Driving inputs for the animation state machine.
///
/// Read from the ECS each frame.  `moveMode` is the integer value of
/// `MoveMode` from PlayerVisState (avoids a header dependency here).
struct AnimationInputs
{
    glm::vec3 velocityWorld{0.0f};    ///< World-space velocity (u/s).
    float yawRad = 0.0f;              ///< Player yaw (radians).
    float pitchRad = 0.0f;            ///< Player pitch (radians, positive = looking down).
    float spineBendMultiplier = 1.0f; ///< Per-weapon-class scaler on Phase F spine bend (1.0 = full).
    float hipLeanMultiplier = 0.0f;   ///< Per-weapon-class hip-lean magnitude (radians of pelvis pitch per radian of
                                      ///< camera pitch, opposite sign).
    float dtSec =
        0.0f; ///< Frame delta time (s). Used by the breathing oscillator and recoil decay in `runSamplingAndSkinning`.
    bool grounded = false;  ///< Touching the ground this tick.
    bool sprinting = false; ///< Sprint key currently held.
    bool crouching = false; ///< Crouch currently held (phase 1: note-only).
    int moveMode = 0;       ///< MoveMode value: 0=OnFoot, 1=Sliding, 2=WallRunning.
    int wallRunSide = 0;    ///< WallSide value: 0=None, 1=Left, 2=Right.
    /// @brief Full-body emote clip to force this frame, as the integer value of a
    /// `ClipId`. Values < `ClipId::_Count` select an emote that overrides
    /// locomotion (played at full weight through the override slot); the default
    /// `_Count` means "no emote".
    int emoteClip = static_cast<int>(ClipId::_Count);
};

/// Number of sampler slots available for the per-frame blend.
///
///  [0] locomotion primary    (Idle / Walk / Run / RunBackward)
///  [1] locomotion secondary  (for 1-D speed band blend)
///  [2] locomotion strafe     (StrafeLeft/Right, WalkStrafe/RunStrafe)
///  [3] override              (Slide / WallRun / Jump / debug clip)
///  [4] transition            (short start / stop / pivot overlay)
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

/// @brief Number of fingers per hand. Iteration count for the FK finger pose
/// applied in CharacterAnimator::applyWeaponHoldPose.
static constexpr size_t kHandFingerIkCount = 5;

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

    /// @brief PR-29: render the entity at server-authoritative animation state.
    ///
    /// Bypasses the per-instance state machine (steps 1-6 of `update()`)
    /// — instead writes the supplied `AnimSnapshot` directly into the
    /// internal samplers and runs the ozz sampling + blending +
    /// LocalToModel pipeline on top.  Used by the client renderer for
    /// REMOTE players whose animation state is replicated from the
    /// server via the snapshot stream; eliminates the residual ~0.4-
    /// median anim-state drift PR-27a's telemetry caught (server runs
    /// animator at 128 Hz, client at 30 Hz; both at 1.0× speed but
    /// per-clip start-time offsets persist for the lifetime of each
    /// clip).
    ///
    /// The local player still uses `update()` — its prediction-driven
    /// state machine is authoritative client-side.
    ///
    /// `inputs` is still consulted for per-frame post-processing that
    /// has no analog in the snapshot (head-pitch transform, wallrun
    /// mirror).  Pass the SAME interp-delayed inputs you would have
    /// passed to `update()`.
    void renderFromServer(const AnimSnapshot& serverState, const AnimationInputs& inputs);

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

    /// @brief Pose both arms (and the gun-holding fingers) for a held weapon.
    ///
    /// Pure forward kinematics — no IK solving. Each bone in the chain
    /// Shoulder → UpperArm → ForeArm → Hand (plus every finger joint) is driven
    /// to its authored local rotation from `pose`, processed root → tip so the
    /// chain stays connected. `weight` ∈ [0,1] slerps from the sampled animation
    /// toward the authored hold pose (used for the weapon-swap fade); 1.0 fully
    /// replaces the arm animation with the static hold.
    ///
    /// Mutates `jointModelMatrices()` in place and recomputes `skinMatrices()`
    /// at the end. Call once per frame after `update()`/`renderFromServer()`.
    void applyWeaponHoldPose(const WeaponHoldPose& pose, float weight);

    /// @brief Recompute `skinMatrices()` = jointModelMats * inverseBindMats.
    /// The LBS skinning palette and hitbox-tracking code both read these.
    void updateSkinMatrices();

    /// @brief Phase F additive recoil kick.
    ///
    /// Pushes an instantaneous upward pitch impulse onto the spine that decays
    /// exponentially over ~250 ms (impl detail). Called once per fired shot
    /// from the client's weapon system; the animator integrates the decay each
    /// frame inside `runSamplingAndSkinning`.
    ///
    /// `strengthRad` is the peak additional pitch in radians, typically
    /// 0.05–0.15 (≈3–8 degrees) for a rifle and larger for heavier weapons.
    void applyRecoilImpulse(float strengthRad);

    /// @brief Number of joints in the underlying rig.
    [[nodiscard]] int numJoints() const noexcept;

    /// @brief Current high-level animator mode as the underlying int of the
    /// private Mode enum (0 = Locomotion, then Crouch, Airborne, Slide, WallRun,
    /// …). Callers should treat the value as opaque.
    [[nodiscard]] int currentModeValue() const noexcept;

    /// @brief Freeze animation playback. While frozen, `update()` and
    /// `renderFromServer()` skip the state-machine + sampler-time updates,
    /// but still run the ozz sample → blend → LocalToModel pipeline using
    /// the last-known sampler state. This keeps the IK pass's base pose
    /// reproducible and stable — needed so the world-space anchor sliders
    /// don't drift from the idle bob while authoring. The procedural
    /// overlays (spine bend etc.) keep responding to live `inputs` so the
    /// gun still tracks aim pitch when the user pans the camera.
    void setFrozen(bool frozen) noexcept;

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
    /// @brief PR-29: shared back end for `update()` and
    /// `renderFromServer()`.  Reads `impl_->samplers` (already
    /// populated by either the state machine or a server snapshot)
    /// and runs the ozz sampling + blending + LocalToModel +
    /// skin-matrix-compose pipeline (steps 7-10 of the original
    /// `update()`).  Consumes `inputs` only for head-pitch and
    /// wallrun-mirror post-processing.
    void runSamplingAndSkinning(const AnimationInputs& inputs);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// @brief Grounding reference that aligns the IDLE pose's feet with the floor.
///
/// The renderer grounds a character by aligning the rig's bind-pose lowest
/// vertex (`bindMeshMinY`) with the bottom of the collision AABB. Mixamo clips
/// bend the knees slightly, so the *animated* feet sit a constant amount above
/// the straight-legged T-pose bind — grounding off the bind makes every clip
/// appear lifted by that amount. This samples the Idle clip once and returns
/// `bindMeshMinY` shifted by the idle-vs-bind foot-joint Y delta, so all clips
/// (which share the idle floor reference) sit on the ground. Falls back to
/// `bindMeshMinY` if the rig has no recognisable foot joints or no Idle clip.
///
/// @param orientationFix  Same rotation passed to CharacterRig::loadFromFBX,
///        so the bind foot positions are measured in the rendered frame.
[[nodiscard]] float computeIdleGroundedMinY(const CharacterRig& rig, const AnimationLibrary& library,
                                            const glm::quat& orientationFix, float bindMeshMinY);
