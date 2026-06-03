/// @file MainMenu.hpp
/// @brief Landing menu screen for top-level navigation.

#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "menus/main/ui/MainMenuUI.hpp"
#include "renderer-new/NewRenderer.hpp"

/// @brief IScreen implementation for the first landing menu.
class MainMenu : public IScreen
{
public:
    /// @brief Bind renderer and window; must be called before iterate().
    /// @return False if either pointer is null.
    bool init(AppContext& ctx);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    /// @brief True if the user requested opening the home/join screen, then clear that request.
    bool consumePlayRequest();

    /// @brief True if the user requested opening the host configuration screen, then clear that request.
    bool consumeHostRequest();

    /// @brief True if the user requested closing the application, then clear that request.
    bool consumeExitRequest();

private:
    NewRenderer* renderer = nullptr; ///< Renderer; not owned.
    SDL_Window* window = nullptr;    ///< Application window; not owned.
    bool pendingPlay = false;        ///< Set when the user clicks "Play", cleared by App.
    bool pendingHost = false;        ///< Set when the user clicks "Host", cleared by App.
    bool pendingExit = false;        ///< Set when the user clicks "Exit", cleared by App.
};
