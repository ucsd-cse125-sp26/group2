/// @file AppContext.hpp
/// @brief Borrowed dependencies shared by client screens.

#pragma once

#include "DeveloperConfig.hpp"
#include "config/UserSettings.hpp"
#include "host/HostedServer.hpp"
#include "network/Client.hpp"
#include "network/NetworkConfig.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <SDL3/SDL_video.h>

#include <string_view>

/// @brief Non-owning view of App-owned services and configuration.
struct AppContext
{
    SDL_Window& window;                 ///< Main application window.
    NewRenderer& renderer;              ///< Shared renderer owned by App.
    Client& client;                     ///< Shared network client owned by App.
    HostedServer& hostedServer;         ///< Local hosted server process owned by App.
    HostConfigState& hostConfigState;   ///< Persistent draft settings for the host config screen.
    NetworkConfig& networkConfig;       ///< Runtime network/discovery config.
    DeveloperConfig& developerConfig;   ///< Developer workflow config.
    UserSettings& userSettings;         ///< User-specific input and gameplay settings.
    std::string_view userSettingsPath;  ///< Save path for user-specific settings.
    std::string_view currentServerName; ///< Display name for the connected server, if known.
};
