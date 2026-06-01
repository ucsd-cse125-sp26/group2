#pragma once

#include "InputBindings.hpp"

#include <string>

namespace user_settings
{
/// @brief Reference mouse hardware used to map default sensitivity to cm/360.
inline constexpr float kReferenceMouseDpi = 800.0f;
/// @brief Default mouse travel for one full turn at the reference DPI.
inline constexpr float kDefaultMouseCmPer360 = 30.0f;
inline constexpr float kPi = 3.14159265358979323846f;
/// @brief Mouse-look sensitivity in radians per SDL relative mouse unit.
inline constexpr float kDefaultMouseSensitivity =
    (2.0f * kPi) / ((kDefaultMouseCmPer360 / 2.54f) * kReferenceMouseDpi);
/// @brief Settings-slider minimum. The maximum is mirrored so default sits centered.
inline constexpr float kMinMouseSensitivity = 0.0001f;
inline constexpr float kMaxMouseSensitivity = 2.0f * kDefaultMouseSensitivity - kMinMouseSensitivity;
/// @brief UI scale for displaying tiny rad/pixel sensitivity values legibly.
inline constexpr float kMouseSensitivityDisplayScale = 1000.0f;
} // namespace user_settings

/// @brief Per-user gameplay settings loaded from SDL's pref-path TOML file.
struct UserSettings
{
    InputBindings inputBindings{InputBindings::defaults()}; ///< Configurable keyboard/mouse and controller bindings.
    float mouseSensitivity{user_settings::kDefaultMouseSensitivity}; ///< Mouse-look sensitivity in radians per pixel.
    float horizontalFovDegrees{110.0f};                      ///< Player-facing horizontal camera FOV in degrees.
    bool showControllerBindings{false};                     ///< Settings page shows controller bindings when true.

    float gamepadYawSensitivity{6.0f};                      ///< radians/sec
    float gamepadPitchSensitivity{6.0f};
    float gamepadLookDeadzone{0.12f}; ///< Gamepad look deadzone radius in [0, 1]. Ignores stick drift out of the box.
    float gamepadMoveDeadzone{
        0.18f}; ///< Gamepad move deadzone radius in [0, 1]. Higher than look — drift-walking is worse than drift-aim.
    bool aimAssistEnabled{true};
    float aimAssistStrength{1.0f}; ///< Aim assist strength in [0, 1], where 0 is no assist and 1 is full assist.
    bool gamepadSwapSticks{false}; ///< If true, swap the left and right sticks for look and move input.
};

/// @brief User-settings persistence helpers.
namespace user_settings
{
/// @brief Return the full path to the per-user settings TOML file.
std::string getPath();

/// @brief Load settings from disk, falling back to defaults for missing or invalid values.
UserSettings load(const std::string& path);

/// @brief Save settings to disk.
bool save(const std::string& path, const UserSettings& settings);
} // namespace user_settings
