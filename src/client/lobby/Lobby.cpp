/// @file Lobby.cpp
/// @brief Lobby screen implementation: network callback wiring, per-frame rendering, and countdown logic.
#include "Lobby.hpp"

#include "SDL3/SDL_init.h"
#include "SDL3/SDL_timer.h"
#include "ui/LobbyUI.hpp"

#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool Lobby::init(NewRenderer* rendererPtr, SDL_Window* windowPtr, Client* clientPtr)
{
    renderer = rendererPtr;
    window = windowPtr;
    client = clientPtr;
    if (!renderer || !window)
        return false;

    client->onLobbyState([this](const std::vector<LobbyPlayer>& snapshot, ClientId localId) {
        players = snapshot;
        localClientId = localId;
    });

    client->onLobbyUpdate([this](const LobbyUpdateEvent& update) {
        switch (update.type) {
        case LobbyUpdateEvent::Type::PlayerJoined:
            SDL_Log("Lobby: player with clientId %u joined", update.id.value);
            if (std::none_of(
                    players.begin(), players.end(), [id = update.id](const LobbyPlayer& p) { return p.id == id; }))
            {
                players.push_back(LobbyPlayer{update.id});
            }
            break;
        case LobbyUpdateEvent::Type::PlayerLeft:
            SDL_Log("Lobby: player with clientId %u left", update.id.value);
            players.erase(std::remove_if(players.begin(),
                                         players.end(),
                                         [id = update.id](const LobbyPlayer& p) { return p.id == id; }),
                          players.end());
            break;
        case LobbyUpdateEvent::Type::PlayerReady:
            SDL_Log("Lobby: player with clientId %u is now ready", update.id.value);
            for (auto& p : players) {
                if (p.id == update.id) {
                    p.ready = true;
                    break;
                }
            }
            break;
        case LobbyUpdateEvent::Type::PlayerUnready:
            SDL_Log("Lobby: player with clientId %u is now unready", update.id.value);
            for (auto& p : players) {
                if (p.id == update.id) {
                    p.ready = false;
                    break;
                }
            }
            break;
        case LobbyUpdateEvent::Type::PlayerNewHost:
            SDL_Log("Lobby: player with clientId %u is now host", update.id.value);
            for (auto& p : players)
                p.isHost = p.id == update.id;
            break;
        default:
        }
    });

    client->onMatchStateUpdate([this](const MatchStatePacket& packet) {
        if (packet.phase == MatchPhase::LOBBY) {
            startMatchState.reset();
            startCountdownActive = packet.countdownTimer > 0.0f;
            startCountdownRemaining = std::max(packet.countdownTimer, 0.0f);
            lastStartCountdownTickNs = startCountdownActive ? SDL_GetTicksNS() : 0;
            return;
        }

        if (packet.phase == MatchPhase::COUNTDOWN) {
            startCountdownActive = false;
            startCountdownRemaining = 0.0f;
            lastStartCountdownTickNs = 0;
            startMatchState = packet;
            return;
        }

        if (packet.phase != MatchPhase::LOBBY)
            startMatchState = packet;
    });

    if (const auto latestLobbyState = client->getLatestLobbyState()) {
        players = latestLobbyState->first;
        localClientId = latestLobbyState->second;
    }

    return true;
}

SDL_AppResult Lobby::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult Lobby::iterate()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (!client->poll()) {
        return SDL_APP_SUCCESS;
    }

    updateStartCountdown();

    LobbyUIConfig config{
        .players = players,
        .localId = localClientId,
        .isHost = std::any_of(
            players.begin(), players.end(), [this](const LobbyPlayer& p) { return p.id == localClientId && p.isHost; }),
        .canStartMatch = canHostStartMatch() && !startCountdownActive,
        .startCountdownActive = startCountdownActive,
        .startCountdownRemaining = startCountdownRemaining,
    };

    const auto result = lobby_ui::buildPlayerList(config);
    if (result.readyChange) {
        client->sendPlayerReady(*result.readyChange);
    }

    if (result.startMatchClicked) {
        client->sendStartMatch();
    }

    ImGui::Render();

    // Default camera: lobby has no scene, so the renderer just draws sky + ImGui overlay.
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

void Lobby::quit()
{
    if (client) {
        client->onLobbyState({});
        client->onLobbyUpdate({});
        client->onMatchStateUpdate({});
    }
}

bool Lobby::shouldStartMatch() const
{
    return startMatchState.has_value();
}

std::optional<MatchStatePacket> Lobby::consumeStartMatchState()
{
    auto state = startMatchState;
    startMatchState.reset();
    startCountdownActive = false;
    startCountdownRemaining = 0.0f;
    lastStartCountdownTickNs = 0;
    return state;
}

bool Lobby::canHostStartMatch() const
{
    bool sawNonHost = false;
    for (const auto& player : players) {
        if (player.isHost)
            continue;

        sawNonHost = true;
        if (!player.ready)
            return false;
    }

    return sawNonHost;
}

void Lobby::updateStartCountdown()
{
    if (!startCountdownActive)
        return;

    const Uint64 now = SDL_GetTicksNS();
    if (lastStartCountdownTickNs == 0) {
        lastStartCountdownTickNs = now;
        return;
    }

    const float dt = static_cast<float>(now - lastStartCountdownTickNs) / 1'000'000'000.0f;
    lastStartCountdownTickNs = now;
    startCountdownRemaining = std::max(0.0f, startCountdownRemaining - dt);

    if (startCountdownRemaining <= 0.0f) {
        startCountdownRemaining = 0.0f;
    }
}
