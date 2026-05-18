/// @file NetworkConfig.hpp
/// @brief Network configuration loaded from config.toml at startup.

#pragma once

#include <cstdint>
#include <string>

/// @brief Network address parameters.
struct NetworkAddress
{
    std::string host = "127.0.0.1";
    uint16_t port = 9999;
};

/// @brief Server-side replication tuning parameters.
///
/// PR-13 (post server-perf): default snapshot rate is now **128 Hz**
/// (= tick rate).  This is the same cadence Valorant / CS2 / Apex run.
/// Pre-PR-13 default was 32 Hz, chosen as a bandwidth/CPU compromise;
/// PR-10's snapshot delta encoding makes the wire cost of 128 Hz
/// roughly 0.8× the pre-PR-10 32 Hz baseline (4× more snapshots × 0.2×
/// each via delta) — i.e. cheaper than where we started, despite 4×
/// the rate.
///
/// What this buys:
///   - Render delay drops 62.5 ms → 15.6 ms (PR-11 entity interp,
///     2-snapshot default).  Targets feel ~1 frame behind the server,
///     not 4.
///   - Lag-comp resolution improves: rewind tick math now snaps to a
///     ~7.8 ms boundary instead of ~31 ms.  Hits register on the
///     same tick they happened, not the same snapshot interval.
///   - Server CPU: each snapshot is parallel-serialised (PR-8) and
///     the 4× rate increase costs ~4× the broadcastRegistry scope.
///     At 200 bots that scope was 0.79 ms p99 in the 32 Hz tests, so
///     128 Hz pushes it to ~3 ms p99 — still under the 7.8 ms tick.
///
/// Override with `[serverRep] snapshotHz = N` in config.toml or the
/// `SERVER_SNAPSHOT_HZ` env var if you need to claw back bandwidth on
/// a constrained network.  Lower values (e.g. 64 Hz or the legacy
/// 32 Hz) still work — render-delay-interpolation (PR-11) and lag-comp
/// (PR-12) both adapt automatically via the snapshotEveryNTicks field.
///
/// @note On the server side, the runtime still defaults to 32 Hz when
/// the field is loaded from a pre-PR-13 config.toml that explicitly
/// sets `snapshotHz = 32`.  Only the *built-in* default changed.
struct ServerReplicationConfig
{
    /// @brief How often the server emits a registry snapshot.
    /// PR-13 default: 128 Hz (full physics-tick rate).  Tournament-
    /// title cadence; render delay ≈ 16 ms with PR-11's 2-snapshot
    /// interpolation buffer.
    int snapshotHz = 128;
};

/// @brief Phase 3d: per-feature toggles for the UDP transport rollout.
///
/// The transport overhaul is staged across multiple sub-phases (3d-1
/// through 3d-5), each gated by one of these flags. Defaults are
/// conservative — features turn on only after they've been verified
/// stable for at least one release.
struct TransportConfig
{
    /// @brief Stage 3d-1: bind a UDP datagram socket alongside the TCP
    /// socket. Currently no traffic flows through it; later stages move
    /// individual packet types over. Cheap to enable (one socket bind);
    /// off by default until 3d-2 actually uses it.
    bool enableUdpSidecar = true;

    /// @brief Stage 3d-2: send INPUT packets over UDP instead of TCP.
    /// Inputs already carry 5-tick redundancy so single-packet loss is
    /// tolerated by design.
    bool inputsOverUdp = true;

    /// @brief Stage 3d-3: send PING (client→server) and PONG
    /// (server→client) over UDP for accurate RTT measurement that
    /// can't be poisoned by snapshot-stream backlog.
    bool pingOverUdp = true;

    /// @brief Stage 3d-4: route UPDATE_REGISTRY snapshots over UDP
    /// instead of TCP. The server fragments oversize snapshots into
    /// MTU-safe datagrams; the client reassembles via
    /// FragmentReassembler. Drop-stale: a single dropped fragment
    /// loses the snapshot but the next one (~31 ms later at 32 Hz)
    /// arrives independently. Off until 3d-4 is verified — defaulting
    /// off lets the rollout be config-driven.
    bool snapshotsOverUdp = true;

    /// @brief Stage 3d-5: route KILL_EVENT, PARTICLE_SPAWN, and
    /// MATCH_STATE through a reliable-style UDP channel instead of
    /// TCP. Each event is shipped multiple times across consecutive
    /// network cycles for redundancy; client dedups by per-channel
    /// sequence number using a 64-entry sliding-window bitset.
    /// Drops disappear into the next redundant send.
    bool eventsOverUdp = true;
};

/// @brief Global server browser / directory-service settings.
struct GlobalDiscoveryConfig
{
    bool enabled = true;                           ///< Client browser and server publishing toggle.
    bool advertiseServer = true;                   ///< Server auto-publishes itself to the directory.
    std::string directoryHost = "cse125.ucsd.edu"; ///< Central directory host.
    uint16_t directoryTcpPort = 10080;             ///< Directory TCP API port.
    uint16_t directoryUdpPort = 10081;             ///< Directory UDP punch-assist port.
    std::string serverName = "Group 2 Server";     ///< Name advertised by local servers.
    uint8_t maxPlayers = 8;                        ///< Display-only capacity advertised by servers.
    int refreshSeconds = 5;                        ///< Client browser refresh cadence.
    int connectPunchTimeoutMs = 900;               ///< UDP punch-assist window before a direct join attempt.
};

/// @brief Runtime network connection parameters.
///
/// Populated by loadNetworkConfig(). If the config file is absent or a key
/// is missing, the field retains its default value — no exception is thrown.
struct NetworkConfig
{
    NetworkAddress clientNetwork;      ///< Client network config (host and port).
    NetworkAddress serverNetwork;      ///< Server network config (host and port).
    ServerReplicationConfig serverRep; ///< Server-side replication tuning.
    TransportConfig transport;         ///< Phase 3d: UDP transport sub-feature toggles.
    GlobalDiscoveryConfig discovery;   ///< Global server browser and NAT assist.
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
