#pragma once

#include "config/InputBindings.hpp"
#include "config/UserSettings.hpp"

#include <SDL3/SDL_events.h>

#include <optional>
#include <string>
#include <string_view>

struct PauseMenuResult
{
    bool resumeGame = false;
    bool exitToDesktop = false;
    bool settingsApplied = false;
};

/// @brief Lightweight in-game pause overlay.
class PauseMenu
{
public:
    void open();
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] bool isSettingsOpen() const;
    bool handleEscape();
    bool consumeEvent(const SDL_Event& event);
    PauseMenuResult render(UserSettings& settings, std::string_view settingsPath);

private:
    void openSettings(const UserSettings& settings);

    bool menuOpen = false;
    bool settingsOpen = false;
    InputBindings draftBindings = InputBindings::defaults();
    float draftMouseSensitivity = 0.0007f;
    bool dirty = false;
    std::optional<Action> listeningAction;
    std::string statusMessage;
};
