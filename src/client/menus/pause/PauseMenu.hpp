#pragma once

#include "config/InputBindings.hpp"
#include "config/UserSettings.hpp"

#include <SDL3/SDL_events.h>

#include <optional>
#include <string>
#include <string_view>

/// @brief Result commands emitted by one pause-menu render pass.
struct PauseMenuResult
{
    bool resumeGame = false;       ///< Close the pause menu and resume gameplay.
    bool returnToMainMenu = false; ///< Leave the current match and return to the home screen.
    bool exitToDesktop = false;    ///< Request normal SDL application shutdown.
    bool settingsApplied = false;  ///< Settings were applied and gameplay input state should be reset.
};

/// @brief Lightweight in-game pause overlay.
class PauseMenu
{
public:
    /// @brief Open the pause menu on its root page.
    void open();

    /// @brief Close the pause menu and discard any transient settings UI state.
    void close();

    /// @brief Return true when the pause overlay is visible.
    [[nodiscard]] bool isOpen() const;

    /// @brief Return true when the settings page is the active pause-menu page.
    [[nodiscard]] bool isSettingsOpen() const;

    /// @brief Handle Escape according to the current menu page/listening state.
    /// @return True when the caller should resume gameplay; false when Escape was consumed internally.
    bool handleEscape();

    /// @brief Consume SDL input events while the pause menu owns keyboard/mouse focus.
    bool consumeEvent(const SDL_Event& event);

    /// @brief Draw the pause/settings overlay and apply user settings when requested.
    PauseMenuResult render(UserSettings& settings, std::string_view settingsPath);

private:
    /// @brief Enter settings page with a draft copy of the current live settings.
    void openSettings(const UserSettings& settings);

    bool menuOpen = false;                                   ///< True when any pause overlay page is open.
    bool settingsOpen = false;                               ///< True when the settings page is active.
    InputBindings draftBindings = InputBindings::defaults(); ///< Editable binding draft.
    float draftMouseSensitivity = 0.0007f;                   ///< Editable mouse-sensitivity draft.
    bool dirty = false;                                      ///< True when draft settings differ from live settings.
    std::optional<Action> listeningAction;                   ///< Action waiting for the next key/button input.
    std::string statusMessage;                               ///< Save status shown in the settings page.
};
