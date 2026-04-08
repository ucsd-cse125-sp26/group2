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

    // Movement keys
    bool forward{false}; ///< W key.
    bool back{false};    ///< S key.
    bool left{false};    ///< A key.
    bool right{false};   ///< D key.
    bool jump{false};    ///< Space key.
    bool crouch{false};  ///< Left Ctrl key.

    /// @brief Horizontal look angle in radians (accumulated from mouse X deltas).
    float yaw{0.0f};

    /// @brief Vertical look angle in radians, clamped to `[-89°, +89°]` by InputSampleSystem.
    float pitch{0.0f};

    /// @brief Currently always 0; reserved for dynamic movement tilt (wallrun lean, strafe tilt).
    float roll{0.0f};
};
