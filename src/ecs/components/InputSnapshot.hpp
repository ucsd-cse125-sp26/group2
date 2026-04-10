#pragma once

#include <cstdint>

/// @brief One tick of player input, stamped with the tick it was sampled on.
///
/// Sent client → server each tick.
/// Stored in the client's ring buffer for prediction reconciliation.
///
/// `yaw` and `pitch` are absolute orientations in radians, not deltas.
/// The server needs the full orientation to compute `wishDir` correctly.
/// `pitch` is clamped to `[-89°, +89°]` (~1.5533 rad) by InputSampleSystem.
struct InputSnapshot
{
    uint32_t tick{0}; ///< Physics tick this snapshot was sampled on.

    /// Movement keys
    bool forward{false}; ///< W key.
    bool back{false};    ///< S key.
    bool left{false};    ///< A key.
    bool right{false};   ///< D key.
    bool jump{false};    ///< Space key.
    bool crouch{false};  ///< Left Ctrl key.
    bool shooting{false};

    float yaw{0.0f};   ///< Horizontal look angle in radians (accumulated from mouse X deltas).
    float pitch{0.0f}; ///< Vertical look angle in radians, clamped to [-89°, +89°] by InputSampleSystem.
    float roll{0.0f};  ///< Currently always 0; reserved for dynamic movement tilt (wallrun lean, strafe tilt).

    /// @brief Yaw/pitch captured at the start of the most-recent physics tick.
    ///
    /// Used by the renderer to interpolate orientation with the same alpha as
    /// position, keeping camera eye and look-direction on the same timebase.
    /// Without this, yaw snaps to the newest value every frame while the eye
    /// position lags behind by up to one tick — causing objects to jitter on
    /// screen when strafing and rotating simultaneously (orbiting).
    float prevTickYaw{0.0f};
    float prevTickPitch{0.0f};
};
