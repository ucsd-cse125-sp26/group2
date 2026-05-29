/// @file HostConfig.cpp
/// @brief Host-configuration screen lifecycle and App transition requests.

#include "HostConfig.hpp"

#include "network/ServerName.hpp"
#include "ui/HostConfigUI.hpp"

#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool HostConfig::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    client = &ctx.client;
    hostedServer = &ctx.hostedServer;
    draft = &ctx.hostConfigState;
    if (!hostedServer->isRunning() && !draft->useSpecificPort) {
        draft->port = ctx.networkConfig.serverNetwork.port;
    }
    if (const auto latestMatchConfig = client->getLatestMatchConfig()) {
        lastSyncedMatchConfig = latestMatchConfig;
    } else {
        const HostConfigState initialDraft = draftConfig();
        lastSyncedMatchConfig =
            MatchConfig{.killsToWin = initialDraft.killsToWin, .maxPlayers = initialDraft.maxPlayers};
    }
    const HostConfigState initialDraft = draftConfig();
    lastSyncedDiscoverySettings =
        DiscoverySettings{.advertiseGlobal = initialDraft.advertiseGlobal, .advertiseLan = initialDraft.advertiseLan};
    client->onMatchConfig([this](const MatchConfig& config) { lastSyncedMatchConfig = config; });
    return true;
}

SDL_AppResult HostConfig::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    return SDL_APP_CONTINUE;
}

void HostConfig::quit()
{
    if (client) {
        client->onMatchConfig({});
    }
}

SDL_AppResult HostConfig::iterate()
{
    if (!renderer || !hostedServer || !draft)
        return SDL_APP_FAILURE;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const bool ownsLocalProcess = hostedServer->isRunning();
    const bool serverRunning = ownsLocalProcess || (client && client->isConnected());
    if (serverRunning && client && !client->poll()) {
        lastError = "Lost connection to hosted server";
    }
    const bool canManageServer = canManageCurrentServer() || ownsLocalProcess;
    const bool hasUnsavedChanges = serverRunning && hasUnsavedServerChanges();

    HostConfigUIInputs inputs{
        .draft = *draft,
        .serverRunning = serverRunning,
        .canManageServer = canManageServer,
        .ownsLocalProcess = ownsLocalProcess,
        .hasUnsavedServerChanges = hasUnsavedChanges,
        .boundPort = hostedServer->port(),
        .errorMessage = lastError,
    };

    const HostConfigResult result = host_config_ui::buildHostConfigMenu(inputs);
    if (result.launchClicked) {
        lastError.clear();
        pendingLaunch = true;
    }
    if (result.updateClicked) {
        updateServerSettings();
    }
    if (result.shutdownClicked) {
        requestShutdownConfirm();
    }
    if (result.goToLobbyClicked) {
        if (hasUnsavedChanges) {
            requestDiscardMatchChangesConfirm();
        } else {
            pendingGoToLobby = true;
        }
    }
    if (result.backToHomeClicked) {
        pendingBackToHome = true;
    }

    const ConfirmResult confirmResult = confirm_.drawAndPoll();
    if (confirmResult == ConfirmResult::Confirmed) {
        if (pendingConfirmAction == PendingConfirmAction::DiscardMatchChanges) {
            if (lastSyncedMatchConfig) {
                draft->killsToWin = lastSyncedMatchConfig->killsToWin;
                draft->maxPlayers = lastSyncedMatchConfig->maxPlayers;
            }
            if (lastSyncedDiscoverySettings) {
                draft->advertiseGlobal = lastSyncedDiscoverySettings->advertiseGlobal;
                draft->advertiseLan = lastSyncedDiscoverySettings->advertiseLan;
            }
            pendingGoToLobby = true;
        } else if (pendingConfirmAction == PendingConfirmAction::ShutdownServer) {
            lastError.clear();
            pendingShutdown = true;
        }
        pendingConfirmAction = PendingConfirmAction::None;
    } else if (confirmResult == ConfirmResult::Cancelled) {
        pendingConfirmAction = PendingConfirmAction::None;
    }

    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

bool HostConfig::consumeLaunchRequest()
{
    if (!pendingLaunch)
        return false;

    pendingLaunch = false;
    return true;
}

bool HostConfig::consumeShutdownRequest()
{
    if (!pendingShutdown)
        return false;

    pendingShutdown = false;
    return true;
}

bool HostConfig::consumeGoToLobbyRequest()
{
    if (!pendingGoToLobby)
        return false;

    pendingGoToLobby = false;
    return true;
}

bool HostConfig::consumeBackToHomeRequest()
{
    if (!pendingBackToHome)
        return false;

    pendingBackToHome = false;
    return true;
}

HostConfigState HostConfig::draftConfig() const
{
    if (!draft)
        return HostConfigState{
            .port = 9999,
            .useSpecificPort = false,
            .useLegacyTcp = false,
            .persistAfterClientExit = false,
            .advertiseGlobal = true,
            .advertiseLan = true,
            .serverName = std::string(server_name::k_default),
            .killsToWin = 25,
            .maxPlayers = 8,
        };

    HostConfigState result = *draft;
    result.port = std::clamp(result.port, 0, 65535);
    result.serverName = server_name::sanitize(result.serverName);
    result.killsToWin = std::clamp(result.killsToWin, 1, 100);
    result.maxPlayers = std::clamp(result.maxPlayers, 2, 128);
    return result;
}

void HostConfig::setLaunchError(const std::string& error)
{
    lastError = error;
}

bool HostConfig::canManageCurrentServer() const
{
    if (!client)
        return false;

    const auto latestLobbyState = client->getLatestLobbyState();
    if (!latestLobbyState)
        return false;

    const auto& [players, localId] = *latestLobbyState;
    return std::any_of(players.begin(), players.end(), [localId](const LobbyPlayer& player) {
        return player.id == localId && player.isHost;
    });
}

bool HostConfig::hasUnsavedServerChanges() const
{
    if (!draft || !lastSyncedMatchConfig || !lastSyncedDiscoverySettings)
        return false;

    return std::clamp(draft->killsToWin, 1, 100) != lastSyncedMatchConfig->killsToWin ||
           std::clamp(draft->maxPlayers, 2, 128) != lastSyncedMatchConfig->maxPlayers ||
           draft->advertiseGlobal != lastSyncedDiscoverySettings->advertiseGlobal ||
           draft->advertiseLan != lastSyncedDiscoverySettings->advertiseLan;
}

bool HostConfig::updateServerSettings()
{
    if (!client || !draft)
        return false;

    const MatchConfig config{.killsToWin = std::clamp(draft->killsToWin, 1, 100),
                             .maxPlayers = std::clamp(draft->maxPlayers, 2, 128)};
    const DiscoverySettings discoverySettings{.advertiseGlobal = draft->advertiseGlobal,
                                              .advertiseLan = draft->advertiseLan};
    draft->killsToWin = config.killsToWin;
    draft->maxPlayers = config.maxPlayers;
    if (!client->sendMatchConfig(config)) {
        lastError = "Failed to send match settings update";
        return false;
    }
    if (!client->sendDiscoverySettings(discoverySettings)) {
        lastError = "Failed to send discovery settings update";
        return false;
    }

    lastError.clear();
    lastSyncedMatchConfig = config;
    lastSyncedDiscoverySettings = discoverySettings;
    return true;
}

void HostConfig::requestDiscardMatchChangesConfirm()
{
    pendingConfirmAction = PendingConfirmAction::DiscardMatchChanges;
    confirm_.open({.title = "Discard Server Settings?",
                   .message = "You have unsaved server setting changes. Discard them?",
                   .confirmText = "Discard",
                   .cancelText = "Cancel",
                   .confirmIsDanger = true});
}

void HostConfig::requestShutdownConfirm()
{
    pendingConfirmAction = PendingConfirmAction::ShutdownServer;
    confirm_.open({.title = "Shutdown Server?",
                   .message = "Stop this server for all connected players?",
                   .confirmText = "Shutdown",
                   .cancelText = "Cancel",
                   .confirmIsDanger = true});
}
