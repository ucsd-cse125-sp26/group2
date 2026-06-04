/// @file TitleScreen.cpp
/// @brief Landing menu screen lifecycle and frame rendering.

#include "TitleScreen.hpp"

#include "ui/TitleScreenUI.hpp"
#include "util/InputCapture.hpp"

bool TitleScreen::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;

    input_capture::releaseGameplayInputCapture(window);

    return renderer != nullptr && window != nullptr;
}

SDL_AppResult TitleScreen::event(SDL_Event* event)
{
    return processCommonImguiEvent(event);
}

SDL_AppResult TitleScreen::iterate()
{
    if (!renderer)
        return SDL_APP_FAILURE;

    beginMenuFrame(renderer);

    const TitleScreenResult result = title_screen_ui::buildTitleScreen();
    if (result.playClicked) {
        pendingPlay = true;
    }
    if (result.hostClicked) {
        pendingHost = true;
    }
    if (result.settingsClicked) {
        pendingSettings = true;
    }
    if (result.exitClicked) {
        pendingExit = true;
    }

    presentMenuFrame(*renderer);
    return SDL_APP_CONTINUE;
}

void TitleScreen::quit() {}

bool TitleScreen::consumePlayRequest()
{
    if (!pendingPlay)
        return false;

    pendingPlay = false;
    return true;
}

bool TitleScreen::consumeHostRequest()
{
    if (!pendingHost)
        return false;

    pendingHost = false;
    return true;
}

bool TitleScreen::consumeSettingsRequest()
{
    if (!pendingSettings)
        return false;

    pendingSettings = false;
    return true;
}

bool TitleScreen::consumeExitRequest()
{
    if (!pendingExit)
        return false;

    pendingExit = false;
    return true;
}
