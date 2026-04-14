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

    auto net = tbl["network"];

    // .value<T>() returns std::optional<T>; missing key yields nullopt, no throw.
    if (auto v = net["host"].value<std::string>())
        cfg.host = *v;
    if (auto v = net["port"].value<uint16_t>())
        cfg.port = *v;

    return cfg;
}
