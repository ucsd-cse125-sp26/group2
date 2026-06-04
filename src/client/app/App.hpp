/// @file App.hpp
/// @brief Top-level application object owning the window, renderer, and network client.

#pragma once
#include "AppContext.hpp"
#include "DeveloperConfig.hpp"
#include "IScreen.hpp"
#include "config/UserSettings.hpp"
#include "host/HostedServer.hpp"
#include "menus/postmatch/PostMatchResult.hpp"
#include "network/Client.hpp"
#include "network/NetworkConfig.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <SDL3/SDL.h>

#include <future>
#include <memory>
#include <optional>
#include <string>

struct JoinRequest;

/// @brief Root application class; owns shared resources and manages screen transitions.
///
/// Implements the SDL3 app-callback contract (init/event/iterate/quit).  Exactly
/// one IScreen is active at a time; App drives transitions among TitleScreen, MainMenu, host
/// configuration, Lobby, and InGame screens.
class App
{
public:
    /// @brief Initialize SDL, the GPU renderer, ImGui, and the network client.
    /// @return True on success; false if any subsystem fails to initialize.
    bool init();

    /// @brief Forward an SDL event to the active screen.
    /// @return SDL_APP_CONTINUE or SDL_APP_FAILURE.
    SDL_AppResult event(SDL_Event* event);

    /// @brief Tick the active screen and check for pending screen transitions.
    /// @return SDL_APP_CONTINUE or SDL_APP_FAILURE.
    SDL_AppResult iterate();

    /// @brief Shut down all subsystems and release resources.
    void quit();

    /// @brief Named screens the application can display.
    enum class Screen
    {
        TitleScreen, ///< Top-level landing/title screen.
        Settings,    ///< Dedicated front-end settings screen.
        MainMenu,    ///< Join/server-browser main menu screen.
        HostConfig,  ///< Local hosting configuration screen.
        Lobby,       ///< Pre-match lobby waiting room.
        Loading,     ///< Match-loading screen shown before synchronous game init.
        PostMatch,   ///< Dedicated scoreboard shown after a completed match.
        InGame       ///< Active match session.
    };

    /// @brief Destroy the current screen and activate the requested one.
    /// @param next The screen to transition to.
    void transitionTo(Screen next);

private:
    SDL_Window* window = nullptr;    ///< Main application window.
    NewRenderer renderer;            ///< SDL_GPU PBR renderer, shared across screens.
    NetworkConfig networkConfig;     ///< Host/port/transport loaded from config.toml.
    DeveloperConfig developerConfig; ///< Developer toggles loaded from config.toml.
    UserSettings userSettings;       ///< User-specific input and gameplay settings.
    std::string userSettingsPath;    ///< Path used to load and save user settings.
    std::string currentServerName;   ///< Display name for the connected server, if known.
    Client client;                   ///< Network client connected to the authoritative server when in a session.
    HostedServer hostedServer;       ///< Optional local server process launched by the host screen.
    HostConfigState hostConfigState{
        .port = 9999,
        .useSpecificPort = false,
        .useLegacyTcp = false,
        .persistAfterClientExit = false,
        .advertiseGlobal = true,
        .advertiseLan = true,
        .serverName = "Group 2 Server",
        .killsToWin = 10,
        .maxPlayers = 8,
    }; ///< Persistent host screen draft state.

    Screen current = Screen::TitleScreen;                   ///< Which screen is currently active.
    Screen settingsReturnScreen_ = Screen::TitleScreen;     ///< Front-end screen to restore after Settings closes.
    std::unique_ptr<IScreen> screen_;                       ///< Active screen instance.
    bool imguiContextOwned = false;                         ///< True once App has created the ImGui context.
    std::optional<PostMatchResult> pendingPostMatchResult_; ///< Result data used to open the post-match screen.

    struct JoinAttemptResult
    {
        ConnectError error = ConnectError::ConnectFailed;
        std::string serverIp;
        uint16_t serverPort = 0;
        std::string serverName;
    };

    std::future<JoinAttemptResult> joinAttempt_; ///< Background direct/global join attempt, if active.
    std::string joinAttemptLabel_;               ///< Target label displayed by the main menu while joining.

    /// @brief Destroy all subsystems without asserting on partial-init state.
    void cleanup();

    /// @brief Build a borrowed context for screen initialisation.
    AppContext screenContext();

    /// @brief Show a modal message on the active main menu screen, if it is active.
    void showMainMenuPopupMessage(const std::string& message);

    /// @brief Start the non-blocking join worker for a main-menu join request.
    void startJoinAttempt(const JoinRequest& request);

    /// @brief Apply a completed join worker result, if one is ready.
    void pollJoinAttempt();

    /// @brief Wait for any active join worker before shutdown.
    void waitForJoinAttempt();

    /// @brief Ask a locally hosted server to shut down before falling back to process termination.
    bool shutdownHostedServerGracefully();
};
