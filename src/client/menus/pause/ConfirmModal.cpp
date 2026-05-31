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
    ImGui::SetNextWindowSize({360.0f, 0.0f}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(request_.title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("%s", request_.message.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button(request_.cancelText.c_str(), {buttonWidth, 34.0f})) {
            result = ConfirmResult::Cancelled;
        }
        ImGui::SameLine();
        const bool confirmPressed =
            request_.confirmIsDanger ? menu_theme::dangerButton(request_.confirmText.c_str(), {buttonWidth, 34.0f})
                                     : menu_theme::accentButton(request_.confirmText.c_str(), {buttonWidth, 34.0f});
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
