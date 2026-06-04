#include "ConfirmModal.hpp"

#include "menus/MenuTheme.hpp"

#include <imgui.h>
#include <utility>

void ConfirmModal::open(ConfirmRequest request)
{
    request_ = std::move(request);
    open_ = true;
    shouldOpenPopup_ = true;
}

void ConfirmModal::cancel()
{
    open_ = false;
    shouldOpenPopup_ = false;
}

bool ConfirmModal::isOpen() const
{
    return open_;
}

ConfirmResult ConfirmModal::drawAndPoll()
{
    if (!open_)
        return ConfirmResult::Pending;

    if (shouldOpenPopup_) {
        ImGui::OpenPopup(request_.title.c_str());
        shouldOpenPopup_ = false;
    }

    ConfirmResult result = ConfirmResult::Pending;
    ImGui::SetNextWindowSize({420.0f, 0.0f}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(request_.title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        menu_theme::terminalStatusLine("CONFIRMATION REQUIRED", "ESC CANCELS");
        ImGui::TextWrapped("%s", request_.message.c_str());
        menu_theme::terminalSection("COMMANDS");

        const float buttonWidth = ImGui::GetContentRegionAvail().x;
        if (menu_theme::terminalActionRow(request_.cancelText.c_str(), nullptr, {buttonWidth, 34.0f})) {
            result = ConfirmResult::Cancelled;
        }
        const bool confirmPressed = menu_theme::terminalActionRow(
            request_.confirmText.c_str(), nullptr, {buttonWidth, 34.0f}, request_.confirmIsDanger);
        if (confirmPressed) {
            result = ConfirmResult::Confirmed;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            result = ConfirmResult::Cancelled;
        }

        if (result != ConfirmResult::Pending) {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    return result;
}
