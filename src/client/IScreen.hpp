/// @file IScreen.hpp
/// @brief Abstract interface for top-level application screens (lobby, in-game).

#pragma once
#include <SDL3/SDL.h>

/// @brief Interface implemented by each full-screen mode (Lobby, Game).
///
/// App owns one active IScreen at a time and delegates SDL event processing,
/// per-frame iteration, and shutdown to it.
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
