/// @file HostConfig.hpp
/// @brief Host-configuration screen for launching and managing a local server.

#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "host/HostedServer.hpp"
#include "menus/pause/ConfirmModal.hpp"
#include "network/DiscoverySettings.hpp"
#include "network/MatchConfig.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <optional>
#include <string>

/// @brief IScreen implementation for local server launch settings.
class HostConfig : public IScreen
{
public:
    /// @brief Bind App-owned renderer, client, hosted-server, and draft config state.
    /// @return True when the required borrowed services are available.
    bool init(AppContext& ctx);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;

    /// @brief No-op screen shutdown hook; App owns all borrowed services.
    void quit() override;

    /// @brief True if the user requested server launch, then clear that request.
    bool consumeLaunchRequest();

    /// @brief True if the user requested hosted-server shutdown, then clear that request.
    bool consumeShutdownRequest();

    /// @brief True if the user requested entering the hosted lobby, then clear that request.
    bool consumeGoToLobbyRequest();

    /// @brief True if the user requested returning home, then clear that request.
    bool consumeBackToHomeRequest();

    /// @brief Current host-screen draft settings.
    HostConfigState draftConfig() const;

    /// @brief Display a launch or connection error on the host form.
    void setLaunchError(const std::string& error);

private:
    enum class PendingConfirmAction
    {
        None,
        DiscardMatchChanges,
        ShutdownServer,
    };

    /// @brief True if the client is connected and is the current lobby host.
    bool canManageCurrentServer() const;

    /// @brief True if the current draft differs from the last settings sent to or received from the server.
    bool hasUnsavedServerChanges() const;

    /// @brief Send the current host-managed settings to the hosted server.
    bool updateServerSettings();

    /// @brief Ask the host whether to discard unsaved match setting changes before leaving this screen.
    void requestDiscardMatchChangesConfirm();

    /// @brief Ask the host to confirm server shutdown.
    void requestShutdownConfirm();

    NewRenderer* renderer = nullptr;                  ///< Renderer; not owned.
    SDL_Window* window = nullptr;                     ///< Application window; not owned.
    Client* client = nullptr;                         ///< Network client owned by App; not owned.
    HostedServer* hostedServer = nullptr;             ///< Hosted server owned by App; not owned.
    HostConfigState* draft = nullptr;                 ///< Persistent draft state owned by App; not owned.
    std::string lastError;                            ///< Error message shown on the host form; empty when no error.
    std::optional<MatchConfig> lastSyncedMatchConfig; ///< Last match config acknowledged locally as server state.
    std::optional<DiscoverySettings> lastSyncedDiscoverySettings; ///< Last discovery settings acknowledged locally.
    ConfirmModal confirm_; ///< Reusable confirmation modal for discarding unsaved changes.
    PendingConfirmAction pendingConfirmAction = PendingConfirmAction::None; ///< Action to run after modal confirm.
    bool pendingLaunch = false;     ///< Set when the user clicks "Launch", cleared by App.
    bool pendingShutdown = false;   ///< Set when the user clicks "Shutdown", cleared by App.
    bool pendingGoToLobby = false;  ///< Set when the user clicks "Go to Lobby", cleared by App.
    bool pendingBackToHome = false; ///< Set when the user clicks "Back to Main Menu", cleared by App.
};
