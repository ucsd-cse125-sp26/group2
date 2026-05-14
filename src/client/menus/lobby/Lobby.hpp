/// @file Lobby.hpp
/// @brief Pre-match lobby screen: player list, ready-up flow, and match-start handoff.

#pragma once
#include "IScreen.hpp"
#include "network/Client.hpp"
#include "network/lobby/LobbyStatus.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <optional>
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
    bool init(NewRenderer* rendererPtr, SDL_Window* windowPtr, Client* clientPtr);

    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;

    /// @brief Deregister all Client callbacks.
    void quit() override;

    /// @brief True when a non-lobby MatchStatePacket has been received and is ready for handoff.
    bool shouldStartMatch() const;

    /// @brief Take ownership of the pending MatchStatePacket and clear internal countdown state.
    /// @return The packet that triggered the match start, or nullopt if none was pending.
    std::optional<MatchStatePacket> consumeStartMatchState();

    bool consumeReturnToMenu(); ///< True if the user has requested to return to the main menu, then clear that request.

private:
    /// @brief True if the local client is host and all non-host players are ready.
    bool canHostStartMatch() const;

    /// @brief Advance the local countdown timer using SDL_GetTicksNS delta.
    void updateStartCountdown();

    NewRenderer* renderer = nullptr;                 ///< Shared renderer; not owned.
    SDL_Window* window = nullptr;                    ///< Application window; not owned.
    Client* client = nullptr;                        ///< Network client; not owned.
    std::vector<LobbyPlayer> players;                ///< Latest snapshot of connected players.
    ClientId localClientId{-1};                      ///< This client's own ID, set by the server on join.
    std::optional<MatchStatePacket> startMatchState; ///< Set when the server signals a match start.
    bool startCountdownActive = false;               ///< True while the pre-match countdown is ticking.
    float startCountdownRemaining = 0.0f;            ///< Seconds remaining in the countdown.
    Uint64 lastStartCountdownTickNs = 0;             ///< SDL tick timestamp of the last countdown update (ns).
    bool returnToMenu = false;                       ///< Set to true when the user wants to return to the main menu.
};
