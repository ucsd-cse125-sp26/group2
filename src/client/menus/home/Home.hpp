/// @file Home.hpp
/// @brief Main menu screen with server join form.

#pragma once

#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "menus/home/ui/HomeUI.hpp"
#include "network/DiscoveryClient.hpp"
#include "network/NetworkConfig.hpp"
#include "network/discovery/GlobalDiscoveryClient.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

/// @brief Server address and port entered by the user on the home screen.
struct JoinRequest
{
    std::string serverIp;        ///< Hostname or IP address of the target server.
    uint16_t serverPort;         ///< TCP port of the target server.
    uint32_t globalServerId = 0; ///< Directory server id, or 0 for manual join.
    std::string serverName;      ///< Display name shown in the lobby after joining.
};

/// @brief IScreen implementation for the main menu; hosts the server join form.
class Home : public IScreen
{
public:
    /// @brief Bind renderer, window, and discovery configuration; must be called before iterate().
    /// @return False if either pointer is null.
    bool init(AppContext& ctx);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    /// @brief Take the pending join request set when the user clicks "Join", clearing it.
    /// @return The request, or nullopt if none is pending.
    std::optional<JoinRequest> consumeJoinRequest();

    /// @brief Take the pending host request set when the user clicks "Host", clearing it.
    /// @return True if a host request was pending.
    bool consumeHostRequest();

    /// @brief Display an error string on the join form (e.g. from a failed connection attempt).
    void setJoinError(const std::string& error);

    /// @brief Display a modal message on the next home screen frame.
    void setPopupMessage(const std::string& message);

private:
    NewRenderer* renderer = nullptr; ///< Renderer; not owned.
    SDL_Window* window = nullptr;    ///< Application window; not owned.
    GlobalDiscoveryConfig discoveryConfig;
    JoinMenuState joinMenuState;     ///< Mutable state backing the join form widgets.
    std::optional<JoinRequest>
        pendingJoinRequest;          ///< Set when the user clicks "Join", cleared on App transition to Lobby.
    bool pendingHostRequest = false; ///< Set when the user clicks "Host", cleared on App transition.
    std::string joinError;           ///< Error message shown on the join form; empty when no error.
    std::string popupMessage;        ///< Modal message shown once after returning to home.
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
