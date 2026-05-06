#include "Lobby.hpp"

bool Lobby::init(SDL_Window* windowPtr, Client* clientPtr)
{
    this->window = windowPtr;
    this->client = clientPtr;
    return true;
}
