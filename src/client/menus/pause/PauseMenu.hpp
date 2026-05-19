#pragma once

#include <SDL3/SDL_events.h>

struct PauseMenuResult
{
    bool resumeGame = false;
    bool exitToDesktop = false;
};

/// @brief Lightweight in-game pause overlay.
class PauseMenu
{
public:
    void open();
    void close();
    [[nodiscard]] bool isOpen() const;
    bool consumeEvent(const SDL_Event& event);
    PauseMenuResult render();

private:
    bool menuOpen = false;
};
