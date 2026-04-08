#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>

class Client
{
public:
    bool init(const char* addr, Uint16 port);
    void shutdown();
    bool send(const void* data, int size);
    bool poll();

private:
    NET_DatagramSocket* sock = nullptr;
    NET_Address* serverAddr = nullptr;
    Uint16 serverPort = 0;
};
