/// @file MatchController.cpp
/// @brief Implementation of the MatchController system that manages match flow and state.

#include "MatchController.hpp"

#include "ecs/systems/MatchSystem.hpp"

#include <cmath>

void MatchController::update(float deltaTime, Registry& registry, Server& server)
{
    switch (currentPhase) {
    case MatchPhase::LOBBY:
        break;
    case MatchPhase::WARMUP: {
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
        if (systems::handleWinCondition(registry, config.killsToWin)) {
            SDL_Log("MatchController: player has won, ending match");
            currentPhase = MatchPhase::FINISHED;
            countdownTimer = k_finishedDuration;
        }
        break;
    }
    case MatchPhase::FINISHED: {
        countdownTimer -= deltaTime;
        if (countdownTimer <= 0.0f) {
            SDL_Log("MatchController: finished duration elapsed");
            currentPhase = MatchPhase::LOBBY;
            countdownTimer = 0.0f;
            systems::resetStats(registry);
        }
        break;
    }
    default:
        break;
    }

    if (skipLobby && currentPhase == MatchPhase::LOBBY) {
        SDL_Log("MatchController: skip_lobby enabled, starting countdown");
        currentPhase = MatchPhase::COUNTDOWN;
        countdownTimer = k_countdownDuration;
        winnerId = -1;
    }

    broadcastMatchState(server);
}

MatchPhase MatchController::getCurrentPhase()
{
    return currentPhase;
}

void MatchController::hostStartedMatch()
{
    if (currentPhase != MatchPhase::LOBBY)
        return;

    SDL_Log("MatchController: host started match, starting countdown");
    currentPhase = MatchPhase::COUNTDOWN;
    countdownTimer = k_countdownDuration;
    winnerId = -1;
}

void MatchController::setSkipLobby(bool v)
{
    skipLobby = v;
}

int MatchController::getWinnerId()
{
    return winnerId;
}

void MatchController::broadcastMatchState(Server& server)
{
    // PR-4 (server-perf): replicate-on-change. The full match packet
    // is reliable-style (replaceKey set on the per-client queue, so
    // a slow drainer always sees the freshest), and the countdown is
    // smoothly interpolated client-side from a single anchor.
    // Broadcasting it 128 Hz is overkill — for 200 bots it was the
    // dominant tick-time spike in PR-3.
    //
    // Triggers for a broadcast:
    //   - Phase changed (rare; warmup→countdown, etc.)
    //   - Winner changed (≤ once per match)
    //   - Countdown crossed a 1-second boundary (so client UI ticks)
    //   - Heartbeat: at least every 64 ticks (~500 ms) so a fresh
    //     client connecting at any point converges within half a
    //     second.
    constexpr int heartbeatTicks = 64;

    const bool phaseChanged = currentPhase != lastBroadcastPhase;
    const bool winnerChanged = winnerId != lastBroadcastWinnerId;
    const bool countdownCrossedSecond = std::floor(countdownTimer) != std::floor(lastBroadcastCountdown);
    const bool heartbeat = ticksSinceBroadcast >= heartbeatTicks;

    if (!phaseChanged && !winnerChanged && !countdownCrossedSecond && !heartbeat) {
        ++ticksSinceBroadcast;
        return;
    }

    MatchStatePacket packet;
    packet.phase = currentPhase;
    packet.countdownTimer = countdownTimer;
    packet.winnerId = winnerId;

    server.broadcastMatchStatus(packet);

    lastBroadcastPhase = currentPhase;
    lastBroadcastWinnerId = winnerId;
    lastBroadcastCountdown = countdownTimer;
    ticksSinceBroadcast = 0;
}

bool MatchController::setKillsToWin(int killsToWin)
{
    if (killsToWin <= 0)
        return false;

    config.killsToWin = killsToWin;
    return true;
}
