/// @file MatchSystem.cpp
/// @brief Implementation of the MatchSystem that checks for match win conditions and updates match state
/// accordingly.

#include "MatchSystem.hpp"

#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/registry/Registry.hpp"

namespace systems
{
bool handleWinCondition(Registry& registry, int killsToWin)
{
    auto view = registry.view<PlayerMatchStats>();
    bool winConditionMet = false;
    for (auto entity : view) {
        if (view.get<PlayerMatchStats>(entity).kills >= killsToWin) {
            winConditionMet = true;
            registry.patch<PlayerMatchStats>(entity, [&](PlayerMatchStats& stats) { stats.hasWon = true; });
        }
    }
    return winConditionMet;
}

void resetStats(Registry& registry)
{
    auto view = registry.view<PlayerMatchStats>();
    for (auto entity : view) {
        registry.patch<PlayerMatchStats>(entity, [](PlayerMatchStats& stats) {
            stats.score = 0;
            stats.kills = 0;
            stats.deaths = 0;
            stats.hasWon = false;
        });
    }
}
} // namespace systems
