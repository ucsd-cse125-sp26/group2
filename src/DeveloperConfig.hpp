/// @file DeveloperConfig.hpp
/// @brief Developer-mode configuration loaded from config.toml.

#pragma once

/// @brief Developer-only runtime toggles shared by client and server.
struct DeveloperConfig
{
    bool skipLobby = false; ///< Bypass lobby flow and start matches directly.
};

/// @brief Load developer config from a TOML file.
///
/// Missing files, missing keys, and parse errors fall back to defaults.
///
/// @param path Path to config.toml.
/// @return Populated DeveloperConfig.
[[nodiscard]] DeveloperConfig loadDeveloperConfig(const char* path);
