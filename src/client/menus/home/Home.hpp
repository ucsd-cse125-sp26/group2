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
};

/// @brief IScreen implementation for the main menu; hosts the server join form.
class Home : public IScreen
{
public:
    /// @brief Bind renderer and window; must be called before iterate().
    /// @return False if either pointer is null.
    bool init(AppContext& ctx);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;

    /// @brief Take the pending join request set when the user clicks "Join", clearing it.
    /// @return The request, or nullopt if none is pending.
    std::optional<JoinRequest> consumeJoinRequest();

    /// @brief Display an error string on the join form (e.g. from a failed connection attempt).
    void setJoinError(const std::string& error);

private:
    NewRenderer* renderer = nullptr; ///< Renderer; not owned.
    SDL_Window* window = nullptr;    ///< Application window; not owned.
    GlobalDiscoveryConfig discoveryConfig;
    JoinMenuState joinMenuState;     ///< Mutable state backing the join form widgets.
    std::optional<JoinRequest>
        pendingJoinRequest;          ///< Set when the user clicks "Join", cleared on App transition to Lobby.
    std::string joinError;           ///< Error message shown on the join form; empty when no error.

    std::unique_ptr<DiscoveryClient> localDiscoveryClient = std::make_unique<DiscoveryClient>();

    void startGlobalRefresh(bool force = false);
    void joinRefreshThreadIfFinished();

    std::vector<net::discovery::ServerInfo> globalServers;
    std::vector<DiscoveryClient::DiscoveredServer> localServers;
    std::string browserError;
    std::mutex browserMutex;
    std::thread browserThread;
    std::atomic<bool> browserRefreshing{false};
    uint64_t lastBrowserRefreshMs = 0;
};
