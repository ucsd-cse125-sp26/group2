/// @file HostedServer.hpp
/// @brief Client-side owner for a locally launched authoritative server process.

#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <cstddef>
#else
#include <sys/types.h>
#endif

/// @brief Persistent host-screen options used when launching a local server.
struct HostConfigState
{
    int port;                    ///< Requested port when useSpecificPort is true; 0 means auto.
    bool useSpecificPort;        ///< True when the user explicitly selected a port.
    bool useLegacyTcp;           ///< True to force the hosted server to legacy TCP transport.
    bool persistAfterClientExit; ///< UI-only persistence request; not wired yet.
    int killsToWin;              ///< Match config: kill threshold to win, sent to the server on launch and update.
};

/// @brief Reserved metadata for an active hosted session.
struct HostSessionInfo
{};

/// @brief Starts, monitors, and shuts down a server process spawned by the client.
class HostedServer
{
public:
    /// @brief Launch the server executable with the requested hosting options.
    /// @param config Host-screen options, including port and transport mode.
    /// @param outError Filled with a user-facing error if launch fails.
    /// @return True once the child server reports its bound port.
    bool start(const HostConfigState& config, std::string& outError);

    /// @brief Stop the hosted server unless it has been detached for persistence.
    void shutdown();

    /// @brief True if the child server process is still running.
    bool isRunning();

    /// @brief Actual server port reported by the child process, or 0 when not running.
    uint16_t port();

private:
    /// @brief Detach the child process so it can continue after the client exits.
    void detachForPersistence();
#if defined(_WIN32)
    void* childProcess = nullptr; ///< Native Windows process handle for the hosted server.
    std::uint32_t childPid = 0;   ///< Process id for diagnostics and process-liveness checks.
#else
    pid_t childPid = -1; ///< POSIX child pid for the hosted server, or -1 when none is running.
#endif
    uint16_t boundPort = 0; ///< Port printed by the child server READY line.
};
