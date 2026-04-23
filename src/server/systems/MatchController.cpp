/// @file MatchController.cpp
/// @brief Implementation of the MatchController system that manages match flow and state.

#include "MatchController.hpp"

#include "ecs/systems/MatchSystem.hpp"

void MatchController::update(float deltaTime, Registry& registry, Server& server)
{
    switch (currentPhase) {
    case MatchPhase::WARMUP: {
        // NOTE: Change in future to support more dynamic match start conditions
        // such as manual start by host, min player count
        if (server.getClientCount() >= 2) {
            SDL_Log("MatchController: enough players connected, starting countdown");
            currentPhase = MatchPhase::COUNTDOWN;
            countdownTimer = k_countdownDuration;
        }
        break;
    }
    case MatchPhase::COUNTDOWN: {
        // NOTE: Can still kill players during countdown.
        countdownTimer -= deltaTime;
        if (countdownTimer <= 0.0f) {
            SDL_Log("MatchController: countdown finished, starting match");
            currentPhase = MatchPhase::IN_PROGRESS;
            countdownTimer = 0.0f;
        }
        break;
    }
    case MatchPhase::IN_PROGRESS: {
        if (systems::handleWinCondition(registry, k_killsToWin)) {
            SDL_Log("MatchController: player has won, ending match");
            currentPhase = MatchPhase::FINISHED;
            countdownTimer = k_finishedDuration;
        }
        break;
    }
    case MatchPhase::FINISHED: {
        // NOTE: Currently resets to WARMUP after fixed duration
        countdownTimer -= deltaTime;
        if (countdownTimer <= 0.0f) {
            SDL_Log("MatchController: finished duration elapsed");
            currentPhase = MatchPhase::WARMUP;
            countdownTimer = 0.0f;
            systems::resetStats(registry);
        }
        break;
    }
    default:
        break;
    }

    broadcastMatchState(server);
}

MatchPhase MatchController::getCurrentPhase()
{
    return currentPhase;
}

int MatchController::getWinnerId()
{
    return winnerId;
}

void MatchController::broadcastMatchState(Server& server)
{
    MatchStatePacket packet;
    packet.phase = currentPhase;
    packet.countdownTimer = countdownTimer;
    packet.winnerId = winnerId;

    server.broadcastMatchStatus(packet);
}
