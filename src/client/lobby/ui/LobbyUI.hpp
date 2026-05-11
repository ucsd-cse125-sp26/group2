#pragma once
#include "network/lobby/LobbyStatus.hpp"

#include <optional>
#include <vector>

namespace lobby_ui
{

std::optional<bool> buildPlayerList(const std::vector<LobbyPlayer>& players, ClientId localId);

} // namespace lobby_ui
