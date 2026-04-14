/// @file NetworkConfig.hpp
/// @brief Network configuration loaded from config.toml at startup.

#pragma once

#include <cstdint>
#include <string>

/// @brief Runtime network connection parameters.
///
/// Populated by loadNetworkConfig(). If the config file is absent or a key
/// is missing, the field retains its default value — no exception is thrown.
struct NetworkConfig
{
    std::string host = "127.0.0.1"; ///< Server hostname or IP address.
    uint16_t port = 9999;           ///< Server TCP port.
};

/// @brief Load network config from a TOML file.
///
/// Attempts to parse @p path as TOML. A missing file or missing key falls back
/// to the default value — no exception escapes this function. On parse error a
/// warning is printed to stderr and all defaults are returned.
///
/// @param path  Path to config.toml (absolute or relative to the working directory).
/// @return      Populated NetworkConfig (defaults for any absent key).
[[nodiscard]] NetworkConfig loadNetworkConfig(const char* path);
