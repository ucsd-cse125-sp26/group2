#include "SDL3_net/SDL_net.h"

class Client
{
public:
    bool init(const char* addr, Uint16 port);
    void shutdown();
    bool send(const void* data, int size);
    bool poll();

private:
    NET_DatagramSocket* sock;
    NET_Address* serverAddr;
    Uint16 serverPort;
};
