#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <cstddef>
#else
#include <sys/types.h>
#endif

struct HostConfigState
{
    int port;                    ///< Requested port when useSpecificPort is true; 0 means auto.
    bool useSpecificPort;        ///< True when the user explicitly selected a port.
    bool useLegacyTcp;           ///< True to force the hosted server to legacy TCP transport.
    bool persistAfterClientExit; ///< UI-only persistence request; not wired yet.
};

struct HostSessionInfo
{};

class HostedServer
{
public:
    bool start(const HostConfigState& config, std::string& outError);
    void shutdown();
    bool isRunning();
    uint16_t port();

private:
    void detachForPersistence();
#if defined(_WIN32)
    void* childProcess = nullptr;
    std::uint32_t childPid = 0;
#else
    pid_t childPid = -1;
#endif
    uint16_t boundPort = 0;
};
