#include "IScreen.hpp"
#include "network/Client.hpp"
class Lobby : public IScreen
{
public:
    bool init(SDL_Window* windowPtr, Client* clientPtr);
    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

private:
    SDL_Window* window = nullptr;
    Client* client = nullptr;
};
