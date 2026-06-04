/// @file LoadingScreen.hpp
/// @brief Terminal-style loading screen shown before synchronous game initialization.

#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <SDL3/SDL.h>

#include <string>

/// @brief Front-end screen displayed while the client is about to enter gameplay.
class LoadingScreen : public IScreen
{
public:
    /// @brief Bind borrowed app resources and capture display metadata.
    bool init(AppContext& ctx);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    /// @brief True after at least one loading frame has been submitted.
    bool readyToStartGame() const;

private:
    NewRenderer* renderer = nullptr; ///< Shared renderer; not owned.
    SDL_Window* window = nullptr;    ///< Application window; not owned.
    std::string serverName;          ///< Display name for the connected server.
    bool renderedFrame = false;      ///< Set once iterate() has submitted one frame.
};
