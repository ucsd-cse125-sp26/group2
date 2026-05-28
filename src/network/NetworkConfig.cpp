/// @file NetworkConfig.cpp
/// @brief Implementation of loadNetworkConfig().

#include "NetworkConfig.hpp"

#include "ServerName.hpp"

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

    // [transport] section — Phase 3d UDP rollout toggles.
    auto transport = tbl["transport"];
    if (auto v = transport["use-udp-sessions"].value<bool>())
        cfg.transport.useUdpSessions = *v;
    if (auto v = transport["allow-legacy-tcp-fallback"].value<bool>())
        cfg.transport.allowLegacyTcpFallback = *v;
    if (auto v = transport["force-relay"].value<bool>())
        cfg.transport.forceRelay = *v;
    if (auto v = transport["enable-udp-sidecar"].value<bool>())
        cfg.transport.enableUdpSidecar = *v;
    if (auto v = transport["inputs-over-udp"].value<bool>())
        cfg.transport.inputsOverUdp = *v;
    if (auto v = transport["ping-over-udp"].value<bool>())
        cfg.transport.pingOverUdp = *v;
    if (auto v = transport["snapshots-over-udp"].value<bool>())
        cfg.transport.snapshotsOverUdp = *v;
    if (auto v = transport["events-over-udp"].value<bool>())
        cfg.transport.eventsOverUdp = *v;

    auto discovery = tbl["global-discovery"];
    if (auto v = discovery["enabled"].value<bool>())
        cfg.discovery.enabled = *v;
    if (auto v = discovery["advertise-server"].value<bool>())
        cfg.discovery.advertiseServer = *v;
    if (auto v = discovery["lan-broadcast-enabled"].value<bool>())
        cfg.discovery.lanBroadcastEnabled = *v;
    if (auto v = discovery["directory-host"].value<std::string>())
        cfg.discovery.directoryHost = *v;
    if (auto v = discovery["directory-tcp-port"].value<uint16_t>())
        cfg.discovery.directoryTcpPort = *v;
    if (auto v = discovery["directory-udp-port"].value<uint16_t>())
        cfg.discovery.directoryUdpPort = *v;
    if (auto v = discovery["server-name"].value<std::string>())
        cfg.discovery.serverName = server_name::sanitize(*v);
    if (auto v = discovery["max-players"].value<int>())
        cfg.discovery.maxPlayers = static_cast<uint8_t>(std::clamp(*v, 2, 128));
    if (auto v = discovery["refresh-seconds"].value<int>())
        cfg.discovery.refreshSeconds = std::clamp(*v, 1, 60);
    if (auto v = discovery["connect-punch-timeout-ms"].value<int>())
        cfg.discovery.connectPunchTimeoutMs = std::clamp(*v, 0, 5000);
    if (auto v = discovery["relay-fallback-delay-ms"].value<int>())
        cfg.discovery.relayFallbackDelayMs = std::clamp(*v, 0, 5000);

    return cfg;
}
