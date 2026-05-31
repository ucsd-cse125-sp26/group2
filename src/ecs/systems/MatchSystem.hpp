/// @file MatchSystem.hpp
/// @brief Match system for handling match logic and win conditions.

#include "ecs/components/ClientId.hpp"
#include "ecs/registry/Registry.hpp"

#include <optional>

namespace systems
{
/// @brief Checks if any player has met win condition and updates their PlayerMatchStats accordingly.
std::optional<ClientId> handleWinCondition(Registry& registry, int killsToWin);

/// @brief Resets all players' match scores to initial values.
void resetStats(Registry& registry);
} // namespace systems
