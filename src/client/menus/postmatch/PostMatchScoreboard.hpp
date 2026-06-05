/// @file PostMatchScoreboard.hpp
/// @brief Dedicated post-match scoreboard screen.

#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "menus/postmatch/PostMatchResult.hpp"
#include "menus/settings/SystemMenuOverlay.hpp"
#include "network/Client.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <SDL3/SDL.h>

#include <string_view>

class PostMatchScoreboard : public IScreen
{
public:
    bool init(AppContext& ctx, PostMatchResult result);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    bool consumeReturnToLobby();
    bool consumeReturnToMenu();
    bool consumeServerShutdownNotice();
    bool consumeExitRequest();

private:
    NewRenderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    Client* client = nullptr;
    UserSettings* settings = nullptr;
    std::string_view settingsPath;
    SystemMenuOverlay systemMenu_;
    PostMatchResult result_;
    bool returnToLobby_ = false;
    bool returnToMenu_ = false;
    bool serverShutdownNotice_ = false;
    bool exitRequested_ = false;
};
