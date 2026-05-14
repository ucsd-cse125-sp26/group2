#include "IScreen.hpp"
#include "renderer-new/NewRenderer.hpp"
class Home : public IScreen
{
public:
    bool init(NewRenderer* rendererPtr, SDL_Window* windowPtr);
    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

private:
    NewRenderer* renderer = nullptr; ///< Renderer; not owned.
    SDL_Window* window = nullptr;    ///< Application window; not owned.
};
