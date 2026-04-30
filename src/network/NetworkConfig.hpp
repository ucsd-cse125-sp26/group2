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

/// @brief Runtime network connection parameters.
///
/// Populated by loadNetworkConfig(). If the config file is absent or a key
/// is missing, the field retains its default value — no exception is thrown.
struct NetworkConfig
{
    NetworkAddress clientNetwork;      ///< Client network config (host and port).
    NetworkAddress serverNetwork;      ///< Server network config (host and port).
    ServerReplicationConfig serverRep; ///< Server-side replication tuning.
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
