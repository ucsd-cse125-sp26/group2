#pragma once

#include "config/UserSettings.hpp"
#include "menus/pause/ConfirmModal.hpp"
#include "menus/settings/SettingsEditor.hpp"

#include <SDL3/SDL_events.h>

#include <string_view>

/// @brief Commands emitted by one system-menu overlay frame.
struct SystemMenuOverlayResult
{
    bool settingsApplied = false; ///< Settings were applied and saved this frame.
    bool exitToDesktop = false;   ///< User confirmed exiting the application.
};

/// @brief Shared Escape menu for non-gameplay screens.
class SystemMenuOverlay
{
public:
    /// @brief Open the system menu on its root page.
    void open();

    /// @brief Close the menu and discard transient settings state.
    void close();

    /// @brief True while the overlay is active.
    [[nodiscard]] bool isOpen() const;

    /// @brief Handle Escape while the overlay is active.
    void handleEscape(const UserSettings& settings);

    /// @brief Consume input events while the overlay owns focus.
    bool consumeEvent(const SDL_Event& event);

    /// @brief Draw the overlay and return any commands emitted this frame.
    SystemMenuOverlayResult render(UserSettings& settings, std::string_view settingsPath);

private:
    bool open_ = false;
    SettingsEditor settingsEditor_;
    ConfirmModal confirm_;
};
