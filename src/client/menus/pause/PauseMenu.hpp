#pragma once

#include "config/UserSettings.hpp"
#include "menus/pause/ConfirmModal.hpp"
#include "menus/settings/SettingsEditor.hpp"

#include <SDL3/SDL_events.h>

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
    bool handleEscape(const UserSettings& settings);

    /// @brief Consume SDL input events while the pause menu owns keyboard/mouse focus.
    bool consumeEvent(const SDL_Event& event);

    /// @brief Draw the pause/settings overlay and apply user settings when requested.
    PauseMenuResult render(UserSettings& settings, std::string_view settingsPath);

private:
    enum class PendingConfirm
    {
        None,
        LeaveMatch,
        ExitDesktop,
    };

    /// @brief Enter settings page with a draft copy of the current live settings.
    void openSettings(const UserSettings& settings);

    bool menuOpen = false;                                 ///< True when any pause overlay page is open.
    SettingsEditor settingsEditor_;                        ///< Shared tabbed settings page.
    ConfirmModal confirm_;                                 ///< Reusable modal for destructive pause-menu actions.
    PendingConfirm pendingConfirm_ = PendingConfirm::None; ///< Action awaiting confirmation.
};
