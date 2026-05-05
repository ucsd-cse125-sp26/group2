/// @file AnimSnapshot.hpp
/// @brief PR-27 — per-entity animation state snapshot, decoupled from CharacterAnimator.
///
/// Both client and server run the same `CharacterAnimator` state machine;
/// at any instant, an entity's animation is fully described by 5 sampler
/// slots (`ClipSampler[5]` in `client/animation/CharacterAnimator.hpp`).
/// `AnimSnapshot` is the wire-friendly + ECS-friendly mirror of those
/// 5 slots — an `uint8_t` clip ID + two floats per slot.
///
/// Two consumers:
///   1. **Server-side history**: `HitboxHistorySystem` extracts an
///      `AnimSnapshot` from each entity's animator each tick, stores it
///      alongside the historical capsules.  When a shot is rewound, the
///      server can compare its historical snapshot to the client's
///      claimed snapshot to decide whether to accept the client's
///      view of the target's animation pose.
///   2. **Wire format**: `SHOT_INTENT` packet carries the client's
///      `AnimSnapshot` of the target it was shooting at.  20-byte
///      packed payload (4 bytes per slot × 5 slots).
///
/// Per-slot delta semantics:
///   * different active/inactive flags → 0.5 penalty (transition slop)
///   * different clip IDs (both active) → 1.0 penalty (irreconcilable)
///   * same clip, different timeRatio → `|Δt| × max(weight)` (weighted)
///   * same clip, different weight    → `|Δw| × 0.5`         (softer)
///
/// The total delta is dimensionless and uniform across body parts —
/// 0.05 timeRatio drift in a fully-active slot is the same number
/// regardless of which bone moves how far in those 0.05 of clip time.
/// That uniformity is the whole reason this metric beats capsule-
/// position-distance for "is the animation pose close enough?".
///
/// @note Avoids depending on `client/animation/AnimationLibrary.hpp`
/// (which carries ozz transitively): we store `clipIdRaw` as a raw
/// `uint8_t` cast of the `ClipId` enum, so this header compiles into
/// any TU including the bot which links no animation code.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

/// @brief One slot in an entity's blend stack.
struct AnimSlot
{
    /// @brief Raw `uint8_t` cast of `ClipId`.  `0xFF` (== `ClipId::_Count`
    /// today) means "slot inactive".  Stored as raw to avoid pulling
    /// the ozz/animation lib into shared headers.
    std::uint8_t clipIdRaw = 0xFFu;

    /// @brief Normalised playhead position in `[0, 1]` over the clip's
    /// duration.  Matches `ClipSampler::timeRatio`.
    float timeRatio = 0.0f;

    /// @brief Blend weight in `[0, 1]`.  Slots with `weight == 0` are
    /// "inactive" — the animator skips them on sample/blend.
    float weight = 0.0f;
};

/// @brief Snapshot of an entity's full animation state at one instant.
///
/// Five slots match `kNumSamplerSlots` in `CharacterAnimator.hpp`.  The
/// per-slot semantics there:
///   [0] locomotion primary    (Idle / Walk / Run / RunBackward)
///   [1] locomotion secondary  (1-D speed band blend)
///   [2] locomotion strafe     (StrafeLeft/Right / Walk variants)
///   [3] override              (Slide / WallRun / Jump / debug clip)
///   [4] reserved future use   (additive upper-body layer)
struct AnimSnapshot
{
    static constexpr std::size_t k_numSlots = 5;
    std::array<AnimSlot, k_numSlots> slots{};
};

namespace anim_snapshot
{

/// @brief PR-27 animation-state delta.  See `AnimSnapshot.hpp`'s file
/// comment for the formula derivation.  Returns a dimensionless
/// non-negative scalar — 0 means identical state, larger means more
/// drift between `a` and `b`.  Typical "accept" threshold: 0.10.
[[nodiscard]] inline float delta(const AnimSnapshot& a, const AnimSnapshot& b)
{
    float d = 0.0f;
    for (std::size_t i = 0; i < AnimSnapshot::k_numSlots; ++i) {
        const auto& sa = a.slots[i];
        const auto& sb = b.slots[i];
        const bool aActive = sa.weight > 0.0f;
        const bool bActive = sb.weight > 0.0f;
        if (aActive != bActive) {
            // Active/inactive mismatch — typical near a transition edge.
            // Soft penalty so a half-transitioned blend doesn't reject
            // the whole shot.
            d += 0.5f;
            continue;
        }
        if (!aActive)
            continue; // both inactive: nothing to compare.
        if (sa.clipIdRaw != sb.clipIdRaw) {
            // Different clip in the same slot — state-machine
            // disagreement, can't be reconciled by timeRatio fuzziness.
            d += 1.0f;
            continue;
        }
        const float maxW = std::max(sa.weight, sb.weight);
        d += std::abs(sa.timeRatio - sb.timeRatio) * maxW;
        d += std::abs(sa.weight - sb.weight) * 0.5f;
    }
    return d;
}

/// @brief Pack one `AnimSlot` into 4 bytes for wire transmission.
///   * byte 0    : `clipIdRaw` (or 0xFF for inactive)
///   * bytes 1-2 : `timeRatio` quantised to `uint16_t` ([0..1] → [0..65535])
///   * byte 3    : `weight`    quantised to `uint8_t`  ([0..1] → [0..255])
inline void pack(const AnimSlot& s, std::uint8_t out[4])
{
    out[0] = s.clipIdRaw;
    const float tClamped = std::max(0.0f, std::min(1.0f, s.timeRatio));
    const float wClamped = std::max(0.0f, std::min(1.0f, s.weight));
    const auto tq = static_cast<std::uint16_t>(std::lround(tClamped * 65535.0f));
    const auto wq = static_cast<std::uint8_t>(std::lround(wClamped * 255.0f));
    out[1] = static_cast<std::uint8_t>(tq & 0xFFu);
    out[2] = static_cast<std::uint8_t>((tq >> 8) & 0xFFu);
    out[3] = wq;
}

inline AnimSlot unpack(const std::uint8_t in[4])
{
    AnimSlot s;
    s.clipIdRaw = in[0];
    const std::uint16_t tq =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) | static_cast<std::uint16_t>(in[2] << 8));
    s.timeRatio = static_cast<float>(tq) / 65535.0f;
    s.weight = static_cast<float>(in[3]) / 255.0f;
    return s;
}

/// @brief Total wire size of an `AnimSnapshot` (5 slots × 4 bytes).
inline constexpr std::size_t k_wireSize = AnimSnapshot::k_numSlots * 4;

inline void packSnapshot(const AnimSnapshot& snap, std::uint8_t out[k_wireSize])
{
    for (std::size_t i = 0; i < AnimSnapshot::k_numSlots; ++i)
        pack(snap.slots[i], &out[i * 4]);
}

inline AnimSnapshot unpackSnapshot(const std::uint8_t in[k_wireSize])
{
    AnimSnapshot s;
    for (std::size_t i = 0; i < AnimSnapshot::k_numSlots; ++i)
        s.slots[i] = unpack(&in[i * 4]);
    return s;
}

} // namespace anim_snapshot

/// @brief PR-27: transient component placed on the shooter entity for
/// the duration of one `runWeapon` invocation when the matching
/// `SHOT_INTENT` packet was received from that client.
///
/// The server's tick path looks up the intent map by `(shooterClientId,
/// shotInputTick)` BEFORE calling `runWeapon`; if a hit, it emplaces
/// this component.  `WeaponSystem::handleFire` reads it (via
/// `registry.try_get<PendingShotIntent>`) to populate the
/// `ShotResolution`'s client-intent + anim-delta columns.  The
/// component is removed after `runWeapon` returns so it never stays
/// around — only the current tick's shot can read it.
///
/// `received == false` means "no SHOT_INTENT for this tick's shot"
/// (UDP loss, or client wasn't shooting); the component might still
/// be present but the consumer should treat it as absent.
struct PendingShotIntent
{
    bool received = false;
    std::uint16_t targetClientId = 0xFFFFu;
    AnimSnapshot targetAnim{};
};
