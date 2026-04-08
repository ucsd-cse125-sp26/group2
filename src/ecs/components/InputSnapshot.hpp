#pragma once

#include <cstdint>

// One tick of player input, stamped with the tick it was sampled on.
//
// Sent client -> server each tick.
// Stored in the client's ring buffer for prediction reconciliation.
//
// yaw/pitch are absolute orientations in radians, not deltas.
// The server needs the full orientation to compute wishDir correctly.
// pitch is clamped to [-89deg, +89deg] (~ 1.5533 rad) by InputSampleSystem.
struct InputSnapshot
{
    uint32_t tick{0};

    // Movement keys
    bool forward{false};
    bool back{false};
    bool left{false};
    bool right{false};
    bool jump{false};
    bool crouch{false};

    // Camera orientation (radians, absolute)
    float yaw{0.0f};
    float pitch{0.0f};
};
