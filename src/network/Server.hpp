#include "SDL3_net/SDL_net.h"
class Server
{
public:
    bool init(NET_Address*, Uint16 port);
    void run();
    void shutdown();

private:
    NET_Server* server;
}