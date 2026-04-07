#include "SDL3_net/SDL_net.h"
class Server
{
public:
    bool init(const char* addr, Uint16 port, int tickRateMs);
    void shutdown();
    void run();

private:
    NET_DatagramSocket* sock;
    bool running;
    int serverTickRateMs;
};
