#pragma once
#include "network/lobby/LobbyStatus.hpp"

#include <optional>
#include <vector>

struct BuildResult
{
    std::optional<bool> readyChange; // true = ready, false = unready, nullopt = no change
    bool startMatchClicked = false;
};

struct LobbyUIConfig
{
    const std::vector<LobbyPlayer>& players;
    ClientId localId;
    bool isHost;
    bool canStartMatch;
    bool startCountdownActive;
    float startCountdownRemaining;
};

namespace lobby_ui
{

BuildResult buildPlayerList(const LobbyUIConfig& config);

} // namespace lobby_ui
