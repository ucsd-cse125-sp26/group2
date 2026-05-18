/// @file main.cpp
/// @brief Global directory service entry point.

#include "network/GlobalDirectoryServer.hpp"
#include "network/NetworkConfig.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <string>

int main(int argc, char** argv)
{
    SDL_Init(0);
    NET_Init();

    const char* base = SDL_GetBasePath();
    std::string cfgPath = std::string(base ? base : "") + "config.toml";
    const NetworkConfig cfg = loadNetworkConfig(cfgPath.c_str());

    Uint16 tcpPort = cfg.discovery.directoryTcpPort;
    Uint16 udpPort = cfg.discovery.directoryUdpPort;
    if (argc >= 2)
        tcpPort = static_cast<Uint16>(SDL_atoi(argv[1]));
    if (argc >= 3)
        udpPort = static_cast<Uint16>(SDL_atoi(argv[2]));

    GlobalDirectoryServer directory;
    if (!directory.init(nullptr, tcpPort, udpPort)) {
        NET_Quit();
        SDL_Quit();
        return 1;
    }

    directory.run();
    directory.stop();

    NET_Quit();
    SDL_Quit();
    return 0;
}
