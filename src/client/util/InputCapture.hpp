/// @file InputCapture.hpp
/// @brief Acquire and release SDL mouse/keyboard capture across screen transitions.

#pragma once

struct SDL_Window;

namespace input_capture
{

/// @brief Put the window into relative-mouse mode for in-game look, draining any stale state.
///
/// Stops text input (in case ImGui left it on), enables relative mouse mode, pumps events so the
/// platform commits the change, and drains the queued relative delta so the first frame after
/// capture does not see a camera jump.
void acquireGameplayInputCapture(SDL_Window* window);

/// @brief Fully release mouse capture so menu screens get a normal desktop cursor.
///
/// Disables relative mouse mode, drops any window mouse grab, ensures the cursor is visible, and
/// pumps events so the change takes effect before the next screen renders.
void releaseGameplayInputCapture(SDL_Window* window);

} // namespace input_capture
