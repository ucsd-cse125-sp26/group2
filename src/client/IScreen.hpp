/// @file IScreen.hpp
/// @brief Abstract interface for top-level application screens (lobby, in-game).

#pragma once
#include <SDL3/SDL.h>

class NewRenderer;
class SystemMenuOverlay;
struct UserSettings;

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

protected:
    /// @brief Forward event to ImGui and detect SDL_EVENT_QUIT.
    /// @return SDL_APP_SUCCESS on quit; SDL_APP_CONTINUE otherwise. Callers
    /// should early-return when the result is not SDL_APP_CONTINUE.
    static SDL_AppResult processCommonImguiEvent(SDL_Event* event);

    /// @brief Handle Escape-toggle of a SystemMenuOverlay and forward consumed events.
    /// @return true if the overlay handled the event and the caller should
    /// return SDL_APP_CONTINUE without further processing.
    static bool handleSystemMenuEvent(SDL_Event* event, SystemMenuOverlay& menu, UserSettings* settings);

    /// @brief Start a new ImGui frame and paint the shared menu background.
    static void beginMenuFrame(NewRenderer* renderer);

    /// @brief Render the ImGui draw data and present a default-camera frame.
    static void presentMenuFrame(NewRenderer& renderer);
};
