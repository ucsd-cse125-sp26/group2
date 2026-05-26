#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "host/HostedServer.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <string>

class HostConfig : public IScreen
{
public:
    bool init(AppContext& ctx);
    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    bool consumeLaunchRequest();
    bool consumeShutdownRequest();
    bool consumeGoToLobbyRequest();
    bool consumeBackToHomeRequest();

    HostConfigState draftConfig() const;
    void setLaunchError(const std::string& error);

private:
    NewRenderer* renderer = nullptr;      ///< Renderer; not owned.
    SDL_Window* window = nullptr;         ///< Application window; not owned.
    Client* client = nullptr;             ///< Network client owned by App; not owned.
    HostedServer* hostedServer = nullptr; ///< Hosted server owned by App; not owned.
    HostConfigState* draft = nullptr;     ///< Persistent draft state owned by App; not owned.
    std::string lastError;                ///< Error message shown on the host form; empty when no error.
    bool pendingLaunch = false;           ///< Set when the user clicks "Launch", cleared by App.
    bool pendingShutdown = false;         ///< Set when the user clicks "Shutdown", cleared by App.
    bool pendingGoToLobby = false;        ///< Set when the user clicks "Go to Lobby", cleared by App.
    bool pendingBackToHome = false;       ///< Set when the user clicks "Back to Main Menu", cleared by App.
};
