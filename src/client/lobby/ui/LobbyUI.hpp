#pragma once
#include "network/lobby/LobbyStatus.hpp"

#include <vector>

namespace lobby_ui
{

void buildPlayerList(const std::vector<LobbyPlayer>& players, ClientId localId);

} // namespace lobby_ui
