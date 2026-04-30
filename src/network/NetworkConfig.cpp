/// @file NetworkConfig.cpp
/// @brief Implementation of loadNetworkConfig().

#include "NetworkConfig.hpp"

#include <algorithm>
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

    // [server-replication] section — Phase 4 snapshot rate.
    auto serverRep = tbl["server-replication"];
    if (auto v = serverRep["snapshot-hz"].value<int>()) {
        // Clamp to a sane range. Above tick-rate makes no sense; below 1 Hz
        // breaks gameplay. Bot-stress tests on loopback typically run at 32;
        // playable settings probably want 32-64 once Phase 5 lands.
        cfg.serverRep.snapshotHz = std::max(1, std::min(*v, 256));
    }

    return cfg;
}
