#include "SDL3/SDL_stdinc.h"
#include "SDL3_net/SDL_net.h"
class Server
{
public:
    bool init(const char* addr, Uint16 port, Uint32 tickRateMs);
    void shutdown();
    void run();

private:
    NET_DatagramSocket* sock;
    bool running;
    Uint32 serverTickRateMs;
};
