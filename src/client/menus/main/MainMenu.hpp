/// @file MainMenu.hpp
/// @brief Main menu screen with server join form.

#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "menus/main/ui/MainMenuUI.hpp"
#include "menus/pause/ConfirmModal.hpp"
#include "menus/settings/SystemMenuOverlay.hpp"
#include "network/DiscoveryClient.hpp"
#include "network/DiscoverySettings.hpp"
#include "network/MatchConfig.hpp"
#include "network/NetworkConfig.hpp"
#include "network/discovery/GlobalDiscoveryClient.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/// @brief Server address and port entered by the user on the main menu screen.
struct JoinRequest
{
    std::string serverIp;        ///< Hostname or IP address of the target server.
    uint16_t serverPort;         ///< TCP port of the target server.
    uint32_t globalServerId = 0; ///< Directory server id, or 0 for manual join.
    std::string serverName;      ///< Display name shown in the lobby after joining.
};

/// @brief IScreen implementation for the main menu/join screen; hosts the server join form.
class MainMenu : public IScreen
{
public:
    /// @brief Bind renderer, window, and discovery configuration; must be called before iterate().
    /// @return False if either pointer is null.
    bool init(AppContext& ctx, ServerBrowserTab initialTab = ServerBrowserTab::LocalListing);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    /// @brief Take the pending join request set when the user clicks "Join", clearing it.
    /// @return The request, or nullopt if none is pending.
    std::optional<JoinRequest> consumeJoinRequest();

    /// @brief Take the pending host request set when the user clicks "Host", clearing it.
    /// @return True if a host request was pending.
    bool consumeHostRequest();

    /// @brief Take the pending title-screen request set when the user clicks "Return to Title Screen", clearing it.
    /// @return True if a title-screen request was pending.
    bool consumeReturnToTitleScreenRequest();

    /// @brief True if the user requested closing the application, then clear that request.
    bool consumeExitRequest();

    /// @brief Display an error string on the join form (e.g. from a failed connection attempt).
    void setJoinError(const std::string& error);

    /// @brief Display a launch or connection error on the host tab.
    void setLaunchError(const std::string& error);

    /// @brief Show or clear the in-progress connection indicator.
    void setJoinInProgress(bool joining, const std::string& label = {});

    /// @brief Display a modal message on the next main menu screen frame.
    void setPopupMessage(const std::string& message);

    /// @brief True if the user requested server launch, then clear that request.
    bool consumeLaunchRequest();

    /// @brief True if the user requested hosted-server shutdown, then clear that request.
    bool consumeShutdownRequest();

    /// @brief True if the user requested entering the hosted lobby, then clear that request.
    bool consumeGoToLobbyRequest();

    /// @brief Current host-screen draft settings.
    HostConfigState consumeDraftConfig() const;

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

    /// @brief Current host-screen draft settings, clamped and sanitized.
    HostConfigState draftConfig() const;

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
    NetworkConfig* networkConfig = nullptr;           ///< Runtime network config owned by App; not owned.
    UserSettings* settings = nullptr;                 ///< Live user settings; not owned.
    std::string_view settingsPath;                    ///< Save path for user settings.
    SystemMenuOverlay systemMenu_;                    ///< Shared Escape menu for front-end screens.
    GlobalDiscoveryConfig discoveryConfig;
    JoinMenuState joinMenuState;                      ///< Mutable state backing the join form widgets.
    std::string lastHostError;                        ///< Error message shown on the host form; empty when no error.
    std::optional<MatchConfig> lastSyncedMatchConfig; ///< Last match config acknowledged locally as server state.
    std::optional<DiscoverySettings> lastSyncedDiscoverySettings; ///< Last discovery settings acknowledged locally.
    ConfirmModal confirm_;                                        ///< Reusable confirmation modal for host tab actions.
    PendingConfirmAction pendingConfirmAction = PendingConfirmAction::None; ///< Action to run after modal confirm.
    std::optional<JoinRequest>
        pendingJoinRequest;          ///< Set when the user clicks "Join", cleared on App transition to Lobby.
    bool pendingHostRequest = false; ///< Set when the user clicks "Host", cleared on App transition.
    bool pendingLaunch = false;      ///< Set when the user clicks "Launch", cleared by App.
    bool pendingShutdown = false;    ///< Set when the user clicks "Shutdown", cleared by App.
    bool pendingGoToLobby = false;   ///< Set when the user clicks "Go to Lobby", cleared by App.
    bool pendingReturnToTitleScreenRequest =
        false;                       ///< Set when the user clicks "Return to Title Screen", cleared on transition.
    bool pendingExitRequest = false; ///< Set when the user confirms "Exit to Desktop", cleared by App.
    std::string joinError;           ///< Error message shown on the join form; empty when no error.
    std::string popupMessage;        ///< Modal message shown once after returning to the main menu.
    bool openPopupMessage = false;   ///< True when the modal should be opened next frame.

    std::unique_ptr<DiscoveryClient> localDiscoveryClient = std::make_unique<DiscoveryClient>();

    /// @brief Start an asynchronous global server-browser refresh when allowed by throttling.
    void startGlobalRefresh(bool force = false);

    /// @brief Join the global browser worker once it has finished.
    void joinRefreshThreadIfFinished();

    std::vector<net::discovery::ServerInfo> globalServers;       ///< Latest directory-server browser snapshot.
    std::vector<DiscoveryClient::DiscoveredServer> localServers; ///< Latest LAN browser snapshot.
    std::string browserError;                                    ///< Last global browser error, empty when none.
    std::mutex browserMutex;                                     ///< Guards global browser results from worker thread.
    std::thread browserThread;                                   ///< Worker used for global server-browser requests.
    std::atomic<bool> browserRefreshing{false};                  ///< True while browserThread is fetching.
    uint64_t lastBrowserRefreshMs = 0;                           ///< SDL tick timestamp of the last global refresh.
};
