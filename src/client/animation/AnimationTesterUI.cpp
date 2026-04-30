/// @file AnimationTesterUI.cpp
/// @brief ImGui panel for driving the animation state machine in development.

#include "AnimationTesterUI.hpp"

#include "AnimationLibrary.hpp"
#include "CharacterAnimator.hpp"
#include "ecs/components/AnimatedCharacter.hpp"
#include "ecs/components/LocalPlayer.hpp"

#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

namespace
{
constexpr int k_clipCount = static_cast<int>(ClipId::_Count);

/// @brief Resolve the current target entity from the UI's raw handle.
///
/// Returns the local-player entity when `targetEntityRaw` is -1, otherwise
/// reifies the raw value.  Returns `entt::null` if no match exists in the
/// registry.
entt::entity resolveTarget(entt::registry& registry, int targetEntityRaw)
{
    if (targetEntityRaw < 0) {
        entt::entity found = entt::null;
        for (auto e : registry.view<LocalPlayer, AnimatedCharacter>())
            found = e;
        return found;
    }

    const auto e = static_cast<entt::entity>(static_cast<uint32_t>(targetEntityRaw));
    return registry.valid(e) ? e : entt::null;
}
} // namespace

void buildAnimationTesterUI(AnimationTesterState& state,
                            entt::registry& registry,
                            float& rigScale,
                            float& rigVerticalOffset)
{
    if (!state.show)
        return;

    ImGui::SetNextWindowPos({1230.f, 620.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({420.f, 520.f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Tester", &state.show)) {
        ImGui::End();
        return;
    }

    // Target entity combo
    ImGui::SeparatorText("Target");

    struct TargetEntry
    {
        int raw; ///< -1 = local player sentinel, else entt::entity raw value.
        std::string label;
    };
    std::vector<TargetEntry> targets;
    targets.push_back({-1, "Local Player"});

    for (auto e : registry.view<AnimatedCharacter>()) {
        if (registry.all_of<LocalPlayer>(e))
            continue;
        char buf[48];
        SDL_snprintf(buf, sizeof(buf), "Entity %u", static_cast<uint32_t>(e));
        targets.push_back({static_cast<int>(static_cast<uint32_t>(e)), buf});
    }

    int selectedIdx = 0;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].raw == state.targetEntityRaw) {
            selectedIdx = static_cast<int>(i);
            break;
        }
    }

    if (ImGui::BeginCombo("Entity", targets[static_cast<size_t>(selectedIdx)].label.c_str())) {
        for (size_t i = 0; i < targets.size(); ++i) {
            const bool isSelected = (static_cast<int>(i) == selectedIdx);
            if (ImGui::Selectable(targets[i].label.c_str(), isSelected)) {
                selectedIdx = static_cast<int>(i);
                state.targetEntityRaw = targets[i].raw;
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const entt::entity target = resolveTarget(registry, state.targetEntityRaw);
    const bool hasTarget = (target != entt::null) && registry.all_of<AnimatedCharacter>(target);

    if (!hasTarget) {
        ImGui::TextColored({1.f, 0.5f, 0.3f, 1.f}, "No AnimatedCharacter on target.");
        ImGui::End();
        return;
    }

    auto& ac = registry.get<AnimatedCharacter>(target);
    if (!ac.animator) {
        ImGui::TextColored({1.f, 0.5f, 0.3f, 1.f}, "Animator missing (rig unavailable).");
        ImGui::End();
        return;
    }

    // Clip dropdown + Play/Stop
    ImGui::SeparatorText("Debug Override");

    state.selectedClip = std::clamp(state.selectedClip, 0, k_clipCount - 1);
    if (ImGui::BeginCombo("Clip", clipName(static_cast<ClipId>(state.selectedClip)))) {
        for (int i = 0; i < k_clipCount; ++i) {
            const bool isSelected = (i == state.selectedClip);
            if (ImGui::Selectable(clipName(static_cast<ClipId>(i)), isSelected))
                state.selectedClip = i;
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Play", {80.f, 0.f}))
        ac.animator->setDebugOverride(static_cast<ClipId>(state.selectedClip));
    ImGui::SameLine();
    if (ImGui::Button("Stop", {80.f, 0.f}))
        ac.animator->setDebugOverride(ClipId::_Count);

    const ClipId currentOverride = ac.animator->debugOverride();
    ImGui::Text("Override: %s", (currentOverride == ClipId::_Count) ? "<graph>" : clipName(currentOverride));

    if (ImGui::SliderFloat("Playback ×", &state.playbackSpeedMul, 0.0f, 4.0f, "%.2f"))
        ac.animator->setDebugPlaybackSpeed(state.playbackSpeedMul);

    // Sampler read-back
    ImGui::SeparatorText("Sampler Slots");

    const auto& samplers = ac.animator->samplers();
    constexpr ImGuiTableFlags kTF =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##samplers", 4, kTF)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24.f);
        ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthFixed, 120.f);
        ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < samplers.size(); ++i) {
            const auto& s = samplers[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", i);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(s.active ? clipName(s.id) : "-");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", static_cast<double>(s.weight));
            ImGui::TableSetColumnIndex(3);
            if (s.active)
                ImGui::Text("t=%.2f  ×%.2f", static_cast<double>(s.timeRatio), static_cast<double>(s.playbackSpeed));
            else
                ImGui::TextDisabled("(inactive)");
        }
        ImGui::EndTable();
    }

    // Rendering tunables
    ImGui::SeparatorText("Rendering");

    ImGui::Checkbox("Show local body (third-person debug)", &state.showLocalBody);
    ImGui::SliderFloat("Rig Scale", &rigScale, 0.01f, 5.0f, "%.2f");
    ImGui::SliderFloat("Rig Vertical Offset", &rigVerticalOffset, -200.0f, 200.0f, "%.1f");
    ImGui::Text("Joints: %d", ac.animator->numJoints());

    ImGui::End();
}
