#pragma once
#include "network/lobby/LobbyStatus.hpp"

#include <vector>

namespace lobby_ui
{

void buildPlayerList(const std::vector<LobbyPlayer>& players);

} // namespace lobby_ui
