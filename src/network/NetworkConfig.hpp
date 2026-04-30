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
/// Phase 4: snapshot rate is decoupled from the simulation tick rate. The
/// server still runs physics at `tickHz` (the deterministic-physics tick
/// rate, currently hardcoded to 128 in ServerGame). Registry snapshots are
/// emitted every `1000 / snapshotHz` ms instead of every tick — at the
/// default 32 Hz that's a 4× bandwidth reduction with no change to
/// gameplay behaviour.
///
/// @note Lower snapshot rates (e.g. 32 Hz) make remote-player visual
/// motion noticeably step-y because the client only gets new positions
/// every ~31 ms. Phase 5's RemoteHistory + render-time interpolation is
/// what makes a 32 Hz snapshot rate visually smooth. Until then, set
/// `snapshotHz` to `tickHz` (128) for full visual smoothness, or accept
/// the trade-off in exchange for the bandwidth/CPU win on the server.
struct ServerReplicationConfig
{
    /// @brief How often the server emits a registry snapshot. Plan default: 32 Hz.
    int snapshotHz = 32;
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
