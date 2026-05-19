#include "ConfirmModal.hpp"

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
        if (request_.confirmIsDanger) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.58f, 0.12f, 0.12f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.72f, 0.16f, 0.16f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.45f, 0.08f, 0.08f, 1.0f});
        }
        if (ImGui::Button(request_.confirmText.c_str(), {buttonWidth, 34.0f})) {
            result = ConfirmResult::Confirmed;
        }
        if (request_.confirmIsDanger) {
            ImGui::PopStyleColor(3);
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
