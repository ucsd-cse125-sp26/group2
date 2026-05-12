/// @file App.hpp
/// @brief Top-level application object owning the window, renderer, and network client.

#pragma once
#include "IScreen.hpp"
#include "network/Client.hpp"
#include "network/NetworkConfig.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <SDL3/SDL.h>

#include <memory>

/// @brief Root application class; owns shared resources and manages screen transitions.
///
/// Implements the SDL3 app-callback contract (init/event/iterate/quit).  Exactly
/// one IScreen is active at a time; App drives transitions between Lobby and InGame.
class App
{
public:
    /// @brief Initialize SDL, the GPU renderer, ImGui, and the network client.
    /// @return True on success; false if any subsystem fails to initialize.
    bool init();

    /// @brief Forward an SDL event to the active screen.
    /// @return SDL_APP_CONTINUE or SDL_APP_FAILURE.
    SDL_AppResult event(SDL_Event* event);

    /// @brief Tick the active screen and check for pending screen transitions.
    /// @return SDL_APP_CONTINUE or SDL_APP_FAILURE.
    SDL_AppResult iterate();

    /// @brief Shut down all subsystems and release resources.
    void quit();

    /// @brief Named screens the application can display.
    enum class Screen
    {
        Lobby, ///< Pre-match lobby waiting room.
        InGame ///< Active match session.
    };

    /// @brief Destroy the current screen and activate the requested one.
    /// @param next The screen to transition to.
    void transitionTo(Screen next);

private:
    SDL_Window* window = nullptr;     ///< Main application window.
    NewRenderer renderer;             ///< SDL_GPU PBR renderer, shared across screens.
    NetworkConfig networkConfig;      ///< Host/port/transport loaded from config.toml.
    Client client;                    ///< Network client connected to the authoritative server.

    Screen current = Screen::Lobby;   ///< Which screen is currently active.
    std::unique_ptr<IScreen> screen_; ///< Active screen instance.
    bool imguiContextOwned = false;   ///< True once App has created the ImGui context.

    /// @brief Destroy all subsystems without asserting on partial-init state.
    void cleanup();
};
