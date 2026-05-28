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
    bool advertiseGlobal;        ///< True to publish the hosted server to the global directory.
    bool advertiseLan;           ///< True to respond to LAN discovery requests.
    std::string serverName;      ///< Name advertised in LAN/global server browsers for this hosted session.
    int killsToWin;              ///< Match config: kill threshold to win, sent to the server on launch and update.
    int maxPlayers;              ///< Match config: maximum number of connected players accepted by the server.
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

    /// @brief Stop the locally owned hosted server process.
    void shutdown();

    /// @brief True if the app still owns a child server process.
    bool isRunning();

    /// @brief Actual server port reported by the child process, or 0 when not running.
    uint16_t port();

    /// @brief True when this client has launched a hosted session and still knows its port.
    bool hasSession() const;

    /// @brief Forget hosted-session metadata after a confirmed local or remote shutdown.
    void clearSession();

private:
    /// @brief Drop local ownership of a persistent server after successful launch.
    void detachForPersistence();
#if defined(_WIN32)
    void* childProcess = nullptr; ///< Native Windows process handle for the hosted server.
    std::uint32_t childPid = 0;   ///< Process id for diagnostics and process-liveness checks.
#else
    pid_t childPid = -1; ///< POSIX child pid for the hosted server, or -1 when none is running.
#endif
    uint16_t boundPort = 0; ///< Port printed by the child server READY line.
};
