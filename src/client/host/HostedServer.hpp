#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>

struct HostConfigState
{
    int port; // 0 = auto
    bool persistAfterClientExit;
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
