/// @file InputCapture.cpp
/// @brief Implementation of mouse/keyboard capture helpers used at screen transitions.
#include "InputCapture.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>

namespace input_capture
{

void acquireGameplayInputCapture(SDL_Window* window)
{
    if (!window)
        return;

    SDL_StopTextInput(window);

    if (!SDL_SetWindowRelativeMouseMode(window, true)) {
        SDL_Log("[input] SDL_SetWindowRelativeMouseMode(true) failed: %s", SDL_GetError());
    }

    SDL_PumpEvents();

    float dx = 0.0f;
    float dy = 0.0f;
    SDL_GetRelativeMouseState(&dx, &dy);
}

void releaseGameplayInputCapture(SDL_Window* window)
{
    if (!window)
        return;

    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_SetWindowMouseGrab(window, false);
    SDL_ShowCursor();
    SDL_PumpEvents();
}

} // namespace input_capture
