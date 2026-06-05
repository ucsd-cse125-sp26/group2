/// @file Lobby.hpp
/// @brief Pre-match lobby screen: player list, ready-up flow, and match-start handoff.

#pragma once
#include "IScreen.hpp"
#include "app/AppContext.hpp"
#include "menus/settings/SystemMenuOverlay.hpp"
#include "network/Client.hpp"
#include "network/MatchConfig.hpp"
#include "network/lobby/LobbyStatus.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @brief IScreen implementation for the pre-match lobby.
///
/// Subscribes to lobby-state and match-state callbacks from Client, renders the
/// player list via LobbyUI, and signals App when a match transition is ready.
class Lobby : public IScreen
{
public:
    /// @brief Bind renderer, window, and network client; register lobby callbacks.
    /// @return False if renderer or window are null.
    bool init(AppContext& ctx);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;

    /// @brief Deregister all Client callbacks.
    void quit() override;

    /// @brief True when a non-lobby MatchStatePacket has been received and is ready for handoff.
    bool shouldStartMatch() const;

    /// @brief Take ownership of the pending MatchStatePacket and clear internal countdown state.
    /// @return The packet that triggered the match start, or nullopt if none was pending.
    std::optional<MatchStatePacket> consumeStartMatchState();

    /// @brief True if the user requested returning to the main menu, then clear that request.
    bool consumeReturnToMenu();

    /// @brief True if the host requested HostConfig without shutting down the session, then clear that request.
    bool consumeReturnToHostConfig();

    /// @brief True if returning home because the server connection closed, then clear that reason.
    bool consumeServerShutdownNotice();

    /// @brief True if the user requested closing the application, then clear that request.
    bool consumeExitRequest();

private:
    /// @brief True when at least one non-host is connected and all connected non-host players are ready.
    bool canHostStartMatch() const;

    /// @brief Advance the local countdown timer using SDL_GetTicksNS delta.
    void updateStartCountdown();

    NewRenderer* renderer = nullptr;                 ///< Shared renderer; not owned.
    SDL_Window* window = nullptr;                    ///< Application window; not owned.
    Client* client = nullptr;                        ///< Network client; not owned.
    UserSettings* settings = nullptr;                ///< Live user settings; not owned.
    std::string_view settingsPath;                   ///< Save path for user settings.
    SystemMenuOverlay systemMenu_;                   ///< Shared Escape menu for front-end screens.
    std::vector<LobbyPlayer> players;                ///< Latest snapshot of connected players.
    ClientId localClientId{-1};                      ///< This client's own ID, set by the server on join.
    std::optional<MatchConfig> matchConfig;          ///< Latest match settings received from the server.
    std::optional<MatchStatePacket> startMatchState; ///< Set when the server signals a match start.
    bool startCountdownActive = false;               ///< True while the pre-match countdown is ticking.
    float startCountdownRemaining = 0.0f;            ///< Seconds remaining in the countdown.
    Uint64 lastStartCountdownTickNs = 0;             ///< SDL tick timestamp of the last countdown update (ns).
    bool returnToMenu = false;                       ///< Set to true when the user wants to return to the main menu.
    bool returnToHostConfig = false;                 ///< Set when the host wants to return to HostConfig.
    bool serverShutdownNotice = false;               ///< Set when the server connection closed while in the lobby.
    bool isHosting = false;                          ///< True if App owns a running hosted server.
    std::string serverName;                          ///< Display name for the connected server.
    std::string hostLanIp = "127.0.0.1";             ///< LAN IPv4 shown in the hosting banner.
    uint16_t hostPort = 0;                           ///< Hosted server port shown in the hosting banner.
    bool exitRequested = false;                      ///< Set when the user confirms "Exit to Desktop".
    bool hostAddressesVisible = false;               ///< Local UI flag: show listen/local addresses while hosting.
};
