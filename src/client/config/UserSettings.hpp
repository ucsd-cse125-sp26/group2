#pragma once

#include "InputBindings.hpp"

#include <string>

/// @brief Per-user gameplay settings loaded from SDL's pref-path TOML file.
struct UserSettings
{
    InputBindings inputBindings{InputBindings::defaults()}; ///< Configurable keyboard/mouse and controller bindings.
    float mouseSensitivity{0.0007f};                        ///< Mouse-look sensitivity in radians per pixel.
    float horizontalFovDegrees{110.0f};                     ///< Player-facing horizontal camera FOV in degrees.
    bool showControllerBindings{false};                     ///< Settings page shows controller bindings when true.

    float gamepadYawSensitivity{6.0f};                      ///< radians/sec
    float gamepadPitchSensitivity{6.0f};
    float gamepadLookDeadzone{0.0f};                        ///< Gamepad look deadzone radius in [0, 1].
    float gamepadMoveDeadzone{0.0f};                        ///< Gamepad move deadzone radius in [0, 1].
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
