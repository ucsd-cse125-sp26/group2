// Titandoom — standalone dedicated server
// Usage:  titandoom-server [port]   (default port 27015)
//
// The server uses traditional main() — no window, no GPU, no SDL renderer.
// SDL is initialised only for its logging infrastructure.

#include "server/GameServer.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[])
{
    SDL_SetAppMetadata("Titandoom Server", "0.1.0", "com.titandoom.server");
    SDL_Init(0); // zero flags = logging only

    uint16_t port = k_serverPort;
    if (argc >= 2)
        port = static_cast<uint16_t>(std::atoi(argv[1]));

    SDL_Log("=== Titandoom Dedicated Server v0.1.0 ===");
    SDL_Log("Tick rate : %d Hz", k_serverTickHz);
    SDL_Log("Snapshot  : %d Hz", k_snapshotHz);
    SDL_Log("Max players: %d", k_maxPlayers);
    SDL_Log(
        "Lag comp  : %d ticks (%.1f s) history", k_lagCompTicks, static_cast<double>(k_lagCompTicks) / k_serverTickHz);

    GameServer server;
    if (!server.init(port)) {
        SDL_Log("Server init failed — exiting");
        SDL_Quit();
        return 1;
    }

    server.run(); // blocks forever (Ctrl-C to stop)

    server.shutdown();
    SDL_Quit();
    return 0;
}
