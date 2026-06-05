/// @file AnimationLibrary.hpp
/// @brief Catalog of ozz animation clips shared across all animated entities.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ozz::animation
{
class Animation;
}

class CharacterRig;

/// @brief Enumerated clip IDs.  See clipFile() for the on-disk filenames.
///
/// Any edits here must also extend `clipName()` and `clipFile()` in the .cpp.
enum class ClipId : uint8_t
{
    Idle,
    Walk,
    Run,
    RunBackward,
    SlowRun,
    Slide,
    WallRun,
    Jump,            ///< Airborne jump animation.
    StrafeLeft,      ///< Running strafe left.
    StrafeRight,     ///< Running strafe right.
    StrafeLeftWalk,  ///< Walking strafe left.
    StrafeRightWalk, ///< Walking strafe right.
    TurnLeft90,      ///< 90-degree turn left (standing).
    TurnRight90,     ///< 90-degree turn right (standing).
    CrouchIdle,
    CrouchWalk,
    CrouchWalkLeft,
    CrouchWalkRight,
    CrouchWalkBackward,
    StartForward,
    StartBackward,
    StartLeft,
    StartRight,
    StopForward,
    StopBackward,
    StopLeft,
    StopRight,
    PivotLeft,
    PivotRight,
    // Full-body emotes (one-shot/looping dances played via the override slot).
    EmoteFlair,
    EmoteMaraschino,
    EmoteGangnam,
    EmoteHipHop,
    EmoteNorthernSoul,
    _Count, ///< Sentinel; also used as "no clip / no override".
};

/// @brief Human-readable name for a clip (for UI / logging).
const char* clipName(ClipId id);

/// @brief Filename (relative to assets/animations/) for a clip.
const char* clipFile(ClipId id);

/// @brief Map an emote wheel index (0..emotes::kEmoteCount-1) to its ClipId.
/// Returns `ClipId::_Count` for out-of-range indices (treated as "no emote").
ClipId emoteClipForIndex(int index);

/// @brief Collection of animation clips loaded on top of a shared skeleton.
///
/// Each clip is stored as an owning ozz::animation::Animation keyed by
/// ClipId.  Lookup is O(1) via `get(id)`.
class AnimationLibrary
{
public:
    AnimationLibrary();
    ~AnimationLibrary();
    AnimationLibrary(const AnimationLibrary&) = delete;
    AnimationLibrary& operator=(const AnimationLibrary&) = delete;
    AnimationLibrary(AnimationLibrary&&) noexcept;
    AnimationLibrary& operator=(AnimationLibrary&&) noexcept;

    /// @brief Load one clip from an FBX file onto the rig's skeleton.
    /// @param rig  Source rig (used for joint names + rest poses).
    /// @param id   Which slot the clip occupies.
    /// @param path Absolute path to the FBX file.
    /// @param useRigRestTranslations  Rotation-only retargeting. When true,
    ///        every bone's translation/scale comes from the rig's own rest
    ///        pose and ONLY rotations are taken from the clip. Use when the
    ///        clip and rig are different-proportioned (or different up-axis)
    ///        skeletons: the rig keeps its own bone lengths and stays
    ///        grounded instead of inheriting the clip's hip height. Defaults
    ///        to false (verbatim translations) so same-character rigs keep
    ///        their authored root motion / vertical bob.
    /// @return True on success; false logs and leaves the slot empty.
    bool loadClipFromFBX(const CharacterRig& rig, ClipId id, const std::string& path,
                         bool useRigRestTranslations = false);

    /// @brief True if a clip has been loaded for @p id.
    [[nodiscard]] bool has(ClipId id) const;

    /// @brief Pointer to the loaded clip, or null if not loaded.
    [[nodiscard]] const ozz::animation::Animation* get(ClipId id) const;

    /// @brief Duration of the clip in seconds (0 if not loaded).
    [[nodiscard]] float duration(ClipId id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
