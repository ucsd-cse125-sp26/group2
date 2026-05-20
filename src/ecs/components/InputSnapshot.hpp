/// @file InputSnapshot.hpp
/// @brief Per-tick player input snapshot for networking and prediction.

#pragma once

#include <cstdint>

inline constexpr std::uint8_t kInvalidGrenadeSelectIndex = 0xff;

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
    bool forward{false};                                         ///< W key.
    bool back{false};                                            ///< S key.
    bool left{false};                                            ///< A key.
    bool right{false};                                           ///< D key.
    bool jump{false};                                            ///< Space key.
    bool crouch{false};                                          ///< Left Ctrl key.
    bool sprint{false};                                          ///< Left Shift key.
    bool grapple{false};                                         ///< Middle mouse button / E key.
    bool shooting{false};                                        ///< Primary fire button.
    bool scoped{false};                                          ///< Right click.
    bool reload{false};                                          ///< Reload button.
    bool pickup{false};                                          ///< Pick Up button (f).
    bool switchToPrimary{false};                                 ///< Switch to gun in primary slot.
    bool switchToSecondary{false};                               ///< Switch to gun in secondary slot.
    bool refillAmmo{false};                                      ///< Debug: refill all weapons to full ammo.
    bool killSelf{false};                                        ///< Debug: kill self (rising-edge only).
    bool skipRespawn{false};                                     ///< Skip respawn timer (space while dead).
    bool throwGrenade{false};                                    ///< Quick G press: throw the selected grenade.
    bool grenadeMenuHeld{false};                                 ///< True while the held-G radial menu is open.
    std::uint8_t grenadeSelectIndex{kInvalidGrenadeSelectIndex}; ///< Hovered radial grenade index, or invalid.
    bool ability1{false};                                        ///< Activate ability 1
    bool ability2{false};                                        ///< Activate ability 2
    bool abilitySelectHeld{false};                               ///< True while holding the ability-selection modifier.
    bool abilitySelectLeft{false};      ///< Choose the left pending ability option (edge-triggered).
    bool abilitySelectRight{false};     ///< Choose the right pending ability option (edge-triggered).
    bool debugGrantAbilityLevel{false}; ///< Debug: grant the next ability choice threshold.

    float yaw{0.0f};                    ///< Horizontal look angle in radians (accumulated from mouse X deltas).
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
