/// @file NetworkConfig.cpp
/// @brief Implementation of loadNetworkConfig().

#include "NetworkConfig.hpp"

#include <cstdio>
#include <toml++/toml.hpp>

NetworkConfig loadNetworkConfig(const char* path)
{
    NetworkConfig cfg; // starts with all defaults

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        // Covers both "file not found" and actual parse errors — both are non-fatal.
        std::fprintf(
            stderr, "[config] Warning: could not load '%s' (%s) — using defaults.\n", path, e.description().data());
        return cfg;
    }

    auto clientNet = tbl["client-network"];

    // .value<T>() returns std::optional<T>; missing key yields nullopt, no throw.
    if (auto v = clientNet["host"].value<std::string>())
        cfg.clientNetwork.host = *v;
    if (auto v = clientNet["port"].value<uint16_t>())
        cfg.clientNetwork.port = *v;

    auto serverNet = tbl["server-network"];
    if (auto v = serverNet["host"].value<std::string>())
        cfg.serverNetwork.host = *v;
    if (auto v = serverNet["port"].value<uint16_t>())
        cfg.serverNetwork.port = *v;

    return cfg;
}
