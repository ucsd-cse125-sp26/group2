/// @file SettingsScreen.hpp
/// @brief Dedicated front-end settings screen.

#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "menus/settings/SettingsEditor.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <string_view>

/// @brief IScreen wrapper around the shared tabbed settings editor.
class SettingsScreen : public IScreen
{
public:
    /// @brief Bind app-owned dependencies and open the settings editor.
    bool init(AppContext& ctx);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    /// @brief True if the user requested returning to the previous screen, then clear that request.
    bool consumeBackRequest();

private:
    NewRenderer* renderer = nullptr;  ///< Renderer; not owned.
    SDL_Window* window = nullptr;     ///< Application window; not owned.
    UserSettings* settings = nullptr; ///< Live user settings; not owned.
    std::string_view settingsPath;    ///< Save path for user settings.
    SettingsEditor editor;            ///< Shared settings page implementation.
    bool pendingBack = false;         ///< Set when the page should close, cleared by App.
};
