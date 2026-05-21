#pragma once

#include "InputBindings.hpp"

#include <string>

/// @brief Per-user gameplay settings loaded from SDL's pref-path TOML file.
struct UserSettings
{
    InputBindings inputBindings{InputBindings::defaults()}; ///< Configurable keyboard/mouse and controller bindings.
    float mouseSensitivity{0.0007f};                        ///< Mouse-look sensitivity in radians per pixel.
    float fovDegrees{60.0f};                                ///< Vertical camera field of view in degrees.
    bool showControllerBindings{false};                     ///< Settings page shows controller bindings when true.
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
