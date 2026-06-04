/// @file Lobby.cpp
/// @brief Lobby screen implementation: network callback wiring, per-frame rendering, and countdown logic.
#include "Lobby.hpp"

#include "SDL3/SDL_init.h"
#include "SDL3/SDL_timer.h"
#include "menus/MenuTheme.hpp"
#include "ui/LobbyUI.hpp"
#include "util/InputCapture.hpp"
#include "util/LocalAddress.hpp"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool Lobby::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    client = &ctx.client;
    settings = &ctx.userSettings;
    settingsPath = ctx.userSettingsPath;

    // Defensive: a prior Game screen should have released mouse capture in
    // its quit(), but enforcing it here means the lobby cursor is always free
    // even if a future code path forgets the release.
    input_capture::releaseGameplayInputCapture(window);

    isHosting = ctx.hostedServer.hasSession();
    serverName = std::string(ctx.currentServerName);
    if (serverName.empty() && isHosting) {
        serverName = ctx.hostConfigState.serverName;
    }
    hostPort = ctx.hostedServer.port();
    hostLanIp = isHosting ? local_address::firstLanIPv4() : std::string{};
    hostAddressesVisible = false;

    client->onLobbyState([this](const std::vector<LobbyPlayer>& snapshot, ClientId localId) {
        players = snapshot;
        localClientId = localId;
        if (const auto latestServerName = client->getLatestServerName(); latestServerName && !latestServerName->empty())
            serverName = *latestServerName;
    });

    client->onLobbyUpdate([this](const LobbyUpdateEvent& update) {
        switch (update.type) {
        case LobbyUpdateEvent::Type::PlayerJoined:
            SDL_Log("Lobby: player with clientId %u joined", update.id.value);
            if (std::none_of(
                    players.begin(), players.end(), [id = update.id](const LobbyPlayer& p) { return p.id == id; }))
            {
                LobbyPlayer joined{update.id};
                joined.displayName = update.displayName;
                players.push_back(joined);
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
            break;
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

    client->onMatchConfig([this](const MatchConfig& config) {
        SDL_Log("Lobby: match settings updated: killsToWin=%d maxPlayers=%d", config.killsToWin, config.maxPlayers);
        matchConfig = config;
    });

    if (const auto latestLobbyState = client->getLatestLobbyState()) {
        players = latestLobbyState->first;
        localClientId = latestLobbyState->second;
    }
    if (const auto latestServerName = client->getLatestServerName(); latestServerName && !latestServerName->empty()) {
        serverName = *latestServerName;
    }

    if (const auto latestMatchConfig = client->getLatestMatchConfig()) {
        matchConfig = latestMatchConfig;
    }

    return true;
}

SDL_AppResult Lobby::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    if (settings != nullptr && event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.key == SDLK_ESCAPE)
    {
        if (systemMenu_.isOpen()) {
            systemMenu_.handleEscape(*settings);
        } else {
            systemMenu_.open();
        }
        return SDL_APP_CONTINUE;
    }

    if (systemMenu_.consumeEvent(*event))
        return SDL_APP_CONTINUE;

    return SDL_APP_CONTINUE;
}

SDL_AppResult Lobby::iterate()
{
    if (!client->poll()) {
        SDL_Log("Lobby: lost connection to server; returning to main menu");
        returnToMenu = true;
        serverShutdownNotice = true;
        return SDL_APP_CONTINUE;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    menu_theme::drawBackground(renderer ? renderer->getDevice() : nullptr);

    updateStartCountdown();

    LobbyUIConfig config{
        .players = players,
        .localId = localClientId,
        .isHost = std::any_of(
            players.begin(), players.end(), [this](const LobbyPlayer& p) { return p.id == localClientId && p.isHost; }),
        .canStartMatch = canHostStartMatch() && !startCountdownActive,
        .startCountdownActive = startCountdownActive,
        .startCountdownRemaining = startCountdownRemaining,
        .matchConfig = matchConfig,
        .serverName = serverName,
        .isHosting = isHosting,
        .hostLanIp = hostLanIp,
        .hostPort = hostPort,
        .hostAddressesVisible = hostAddressesVisible,
    };

    const auto result = lobby_ui::buildPlayerList(config);
    if (result.readyChange) {
        client->sendPlayerReady(*result.readyChange);
    }

    if (result.startMatchClicked) {
        client->sendStartMatch();
    }

    if (result.returnToMenuClicked) {
        returnToMenu = true;
    }
    if (result.returnToHostConfigClicked) {
        returnToHostConfig = true;
    }
    if (result.hostAddressesVisibilityToggled) {
        hostAddressesVisible = !hostAddressesVisible;
    }

    if (settings != nullptr) {
        const SystemMenuOverlayResult menuResult = systemMenu_.render(*settings, settingsPath);
        if (menuResult.exitToDesktop) {
            exitRequested = true;
        }
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
        client->onMatchConfig({});
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
    if (players.empty())
        return false;

    for (const auto& player : players) {
        if (player.isHost)
            continue;

        if (!player.ready)
            return false;
    }

    return true;
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

bool Lobby::consumeReturnToMenu()
{
    if (!returnToMenu)
        return false;

    returnToMenu = false;
    return true;
}

bool Lobby::consumeReturnToHostConfig()
{
    if (!returnToHostConfig)
        return false;

    returnToHostConfig = false;
    return true;
}

bool Lobby::consumeServerShutdownNotice()
{
    if (!serverShutdownNotice)
        return false;

    serverShutdownNotice = false;
    return true;
}

bool Lobby::consumeExitRequest()
{
    if (!exitRequested)
        return false;

    exitRequested = false;
    return true;
}
