#include "PauseMenu.hpp"

#include <imgui.h>

void PauseMenu::open()
{
    menuOpen = true;
}

void PauseMenu::close()
{
    menuOpen = false;
}

bool PauseMenu::isOpen() const
{
    return menuOpen;
}

bool PauseMenu::consumeEvent(const SDL_Event& event)
{
    if (!menuOpen)
        return false;

    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_TEXT_INPUT:
        return true;
    default:
        return false;
    }
}

PauseMenuResult PauseMenu::render()
{
    PauseMenuResult result{};
    if (!menuOpen)
        return result;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 150));

    constexpr ImVec2 k_windowSize{320.0f, 190.0f};
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(k_windowSize, ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Paused", nullptr, flags)) {
        ImGui::TextUnformatted("Game Paused");
        ImGui::Separator();

        const float buttonWidth = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("Resume", {buttonWidth, 36.0f})) {
            result.resumeGame = true;
        }

        ImGui::Spacing();
        if (ImGui::Button("Exit to Desktop", {buttonWidth, 36.0f})) {
            result.exitToDesktop = true;
        }
    }
    ImGui::End();

    return result;
}
