/// @file HostConfig.cpp
/// @brief Host-configuration screen lifecycle and App transition requests.

#include "HostConfig.hpp"

#include "network/ServerName.hpp"
#include "ui/HostConfigUI.hpp"
#include "util/InputCapture.hpp"

#include <algorithm>
#include <imgui.h>

bool HostConfig::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    client = &ctx.client;
    hostedServer = &ctx.hostedServer;
    draft = &ctx.hostConfigState;
    settings = &ctx.userSettings;
    settingsPath = ctx.userSettingsPath;

    // Defensive: menus always run with a free desktop cursor.
    input_capture::releaseGameplayInputCapture(window);

    if (!hostedServer->isRunning() && !draft->useSpecificPort) {
        draft->port = ctx.networkConfig.serverNetwork.port;
    }
    if (const auto latestMatchConfig = client->getLatestMatchConfig()) {
        lastSyncedMatchConfig = latestMatchConfig;
    } else {
        const HostConfigState initialDraft = draftConfig();
        lastSyncedMatchConfig = MatchConfig{
            .killsToWin = initialDraft.killsToWin,
            .maxPlayers = initialDraft.maxPlayers,
            .powerupInitialSpawnDelaySeconds = initialDraft.powerupInitialSpawnDelaySeconds,
            .powerupRespawnCooldownSeconds = initialDraft.powerupRespawnCooldownSeconds,
        };
    }
    const HostConfigState initialDraft = draftConfig();
    lastSyncedDiscoverySettings =
        DiscoverySettings{.advertiseGlobal = initialDraft.advertiseGlobal, .advertiseLan = initialDraft.advertiseLan};
    client->onMatchConfig([this](const MatchConfig& config) { lastSyncedMatchConfig = config; });
    return true;
}

SDL_AppResult HostConfig::event(SDL_Event* event)
{
    if (const SDL_AppResult result = processCommonImguiEvent(event); result != SDL_APP_CONTINUE)
        return result;

    handleSystemMenuEvent(event, systemMenu_, settings);
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

    beginMenuFrame(renderer);

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
    if (result.backToMainMenuClicked) {
        pendingBackToMainMenu = true;
    }

    const ConfirmResult confirmResult = confirm_.drawAndPoll();
    if (confirmResult == ConfirmResult::Confirmed) {
        if (pendingConfirmAction == PendingConfirmAction::DiscardMatchChanges) {
            if (lastSyncedMatchConfig) {
                draft->killsToWin = lastSyncedMatchConfig->killsToWin;
                draft->maxPlayers = lastSyncedMatchConfig->maxPlayers;
                draft->powerupInitialSpawnDelaySeconds = lastSyncedMatchConfig->powerupInitialSpawnDelaySeconds;
                draft->powerupRespawnCooldownSeconds = lastSyncedMatchConfig->powerupRespawnCooldownSeconds;
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

    if (settings != nullptr) {
        const SystemMenuOverlayResult menuResult = systemMenu_.render(*settings, settingsPath);
        if (menuResult.exitToDesktop) {
            pendingExitRequest = true;
        }
    }

    presentMenuFrame(*renderer);
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

bool HostConfig::consumeBackToMainMenuRequest()
{
    if (!pendingBackToMainMenu)
        return false;

    pendingBackToMainMenu = false;
    return true;
}

bool HostConfig::consumeExitRequest()
{
    if (!pendingExitRequest)
        return false;

    pendingExitRequest = false;
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
            .powerupInitialSpawnDelaySeconds = 240.0f,
            .powerupRespawnCooldownSeconds = 30.0f,
        };

    HostConfigState result = *draft;
    result.port = std::clamp(result.port, 0, 65535);
    result.serverName = server_name::sanitize(result.serverName);
    result.killsToWin = std::clamp(result.killsToWin, 1, 100);
    result.maxPlayers = std::clamp(result.maxPlayers, 2, 128);
    result.powerupInitialSpawnDelaySeconds = std::clamp(result.powerupInitialSpawnDelaySeconds, 0.0f, 600.0f);
    result.powerupRespawnCooldownSeconds = std::clamp(result.powerupRespawnCooldownSeconds, 1.0f, 300.0f);
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
           std::clamp(draft->powerupInitialSpawnDelaySeconds, 0.0f, 600.0f) !=
               lastSyncedMatchConfig->powerupInitialSpawnDelaySeconds ||
           std::clamp(draft->powerupRespawnCooldownSeconds, 1.0f, 300.0f) !=
               lastSyncedMatchConfig->powerupRespawnCooldownSeconds ||
           draft->advertiseGlobal != lastSyncedDiscoverySettings->advertiseGlobal ||
           draft->advertiseLan != lastSyncedDiscoverySettings->advertiseLan;
}

bool HostConfig::updateServerSettings()
{
    if (!client || !draft)
        return false;

    const MatchConfig config{
        .killsToWin = std::clamp(draft->killsToWin, 1, 100),
        .maxPlayers = std::clamp(draft->maxPlayers, 2, 128),
        .powerupInitialSpawnDelaySeconds = std::clamp(draft->powerupInitialSpawnDelaySeconds, 0.0f, 600.0f),
        .powerupRespawnCooldownSeconds = std::clamp(draft->powerupRespawnCooldownSeconds, 1.0f, 300.0f),
    };
    const DiscoverySettings discoverySettings{.advertiseGlobal = draft->advertiseGlobal,
                                              .advertiseLan = draft->advertiseLan};
    draft->killsToWin = config.killsToWin;
    draft->maxPlayers = config.maxPlayers;
    draft->powerupInitialSpawnDelaySeconds = config.powerupInitialSpawnDelaySeconds;
    draft->powerupRespawnCooldownSeconds = config.powerupRespawnCooldownSeconds;
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
