#include "SDL3_net/SDL_net.h"

class Client
{
public:
    bool init(NET_Address*, Uint16 port);
    void shutdown();
    bool send(const void* data, int size);
    bool poll();

private:
    NET_DatagramSocket* sock;
    NET_Address* server_addr;
    Uint16 server_port;
};
