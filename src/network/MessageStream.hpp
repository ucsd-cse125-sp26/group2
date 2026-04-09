#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <cstring>
#include <functional>
#include <vector>

class MessageStream
{

public:
    NET_StreamSocket* socket = nullptr;

    bool send(const void* data, Uint32 size);
    bool poll(const std::function<void(const void* data, Uint32 size)>& callback);

private:
    std::vector<Uint8> recvBuf;
};
