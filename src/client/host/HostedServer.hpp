#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>

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
    pid_t childPid = -1;
    uint16_t boundPort = 0;
};
