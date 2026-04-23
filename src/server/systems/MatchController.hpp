/// @file MatchControler.hpp
/// @brief Controls match flow, including starting and ending matches and managing match state.
#pragma once

#include "ecs/registry/Registry.hpp"
#include "network/MatchStatus.hpp"
#include "network/Server.hpp"

class MatchController
{
public:
    void update(float deltaTime, Registry& registry, Server& server);
    MatchPhase getCurrentPhase();
    int getWinnerId();

private:
    MatchPhase currentPhase = MatchPhase::WARMUP;
    float countdownTimer = 0.0f;
    int winnerId = -1;

    static constexpr float k_countdownDuration = 5.0f;
    static constexpr float k_finishedDuration = 5.0f;
    static constexpr int k_killsToWin = 10;

    void broadcastMatchState(Server& server);
};
