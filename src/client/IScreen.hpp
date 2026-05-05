#pragma once
#include <SDL3/SDL.h>

class IScreen
{
public:
    virtual ~IScreen() = default;

    /// @brief Handle an incoming SDL event.
    /// @param event The SDL event to process.
    /// @return SDL_APP_CONTINUE to keep running, SDL_APP_FAILURE to exit.
    virtual SDL_AppResult event(SDL_Event* event) = 0;

    /// @brief Update the screen state and render the next frame.
    /// @return SDL_APP_CONTINUE to keep running, SDL_APP_FAILURE to exit.
    virtual SDL_AppResult iterate() = 0;

    /// @brief Perform any necessary cleanup before the screen is destroyed.
    virtual void quit() = 0;

    /// @brief Perform cleanup that must happen after the App-owned renderer shuts down.
    virtual void shutdownAfterRenderer() {}
};
