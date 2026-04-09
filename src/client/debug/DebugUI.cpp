#include "debug/DebugUI.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

bool DebugUI::init(SDL_Window* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        SDL_Log("DebugUI: ImGui_ImplSDL3_InitForSDLGPU failed");
        return false;
    }

    return true;
}

void DebugUI::shutdown()
{
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::processEvent(const SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
}

void DebugUI::newFrame()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::buildUI(const Registry& registry, const int tickCount)
{
    ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({480.0f, 580.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("ECS Inspector");

    // Key bindings reminder
    ImGui::TextDisabled("ESC: toggle mouse  |  Q: quit  |  F1: test packet");
    ImGui::Separator();

    // Component visibility toggles
    ImGui::SeparatorText("Components");
    ImGui::Checkbox("Position", &showPosition);
    ImGui::SameLine();
    ImGui::Checkbox("PrevPosition", &showPrevPosition);
    ImGui::Checkbox("Velocity", &showVelocity);
    ImGui::SameLine();
    ImGui::Checkbox("CollisionShape", &showCollisionShape);
    ImGui::Checkbox("PlayerState", &showPlayerState);
    ImGui::SameLine();
    ImGui::Checkbox("InputSnapshot", &showInputSnapshot);
    ImGui::Checkbox("View Angles", &showViewAngles);

    // Stats bar
    ImGui::Separator();
    ImGui::Text("Physics tick: %d", tickCount);

    const auto* const k_entityStorage = registry.storage<entt::entity>();

    int entityCount = 0;
    if (k_entityStorage)
        for (const auto entity : *k_entityStorage)
            if (registry.valid(entity))
                ++entityCount;

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::Text("Entities: %d", entityCount);
    ImGui::Separator();

    if (!k_entityStorage) {
        ImGui::End();
        return;
    }

    // Per-entity sections
    for (const entt::entity entity : *k_entityStorage) {
        if (!registry.valid(entity))
            continue;

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));

        // Build label — append [LOCAL PLAYER] tag for the locally controlled entity.
        char entityLabel[48];
        const bool k_isLocal = registry.all_of<LocalPlayer>(entity);
        SDL_snprintf(entityLabel,
                     sizeof(entityLabel),
                     k_isLocal ? "Entity #%u  [LOCAL PLAYER]" : "Entity #%u",
                     static_cast<unsigned>(entt::to_integral(entity)));

        if (ImGui::CollapsingHeader(entityLabel, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Vec3 component table
            constexpr ImGuiTableFlags k_tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;

            if (ImGui::BeginTable("##vec3", 4, k_tableFlags)) {
                ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                ImGui::TableHeadersRow();

                const auto vec3Row = [](const char* name, const glm::vec3& v) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%8.3f", static_cast<double>(v.x));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%8.3f", static_cast<double>(v.y));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%8.3f", static_cast<double>(v.z));
                };

                if (showPosition && registry.all_of<Position>(entity))
                    vec3Row("Position", registry.get<Position>(entity).value);
                if (showPrevPosition && registry.all_of<PreviousPosition>(entity))
                    vec3Row("PrevPosition", registry.get<PreviousPosition>(entity).value);
                if (showVelocity && registry.all_of<Velocity>(entity))
                    vec3Row("Velocity", registry.get<Velocity>(entity).value);
                if (showCollisionShape && registry.all_of<CollisionShape>(entity))
                    vec3Row("CollisionShape he", registry.get<CollisionShape>(entity).halfExtents);

                ImGui::EndTable();
            }

            // View angles (degrees — easier to read than radians)
            if (showViewAngles && registry.all_of<InputSnapshot>(entity)) {
                const auto& c = registry.get<InputSnapshot>(entity);
                ImGui::Text("View Angles   yaw: %7.2f°   pitch: %7.2f°   roll: %6.2f°",
                            static_cast<double>(glm::degrees(c.yaw)),
                            static_cast<double>(glm::degrees(c.pitch)),
                            static_cast<double>(glm::degrees(c.roll)));
            }

            // PlayerState
            if (showPlayerState && registry.all_of<PlayerState>(entity)) {
                const auto& c = registry.get<PlayerState>(entity);
                ImGui::Text("PlayerState   grounded:%-3s  crouching:%-3s  sliding:%-3s",
                            c.grounded ? "YES" : "NO",
                            c.crouching ? "YES" : "NO",
                            c.sliding ? "YES" : "NO");
            }

            // InputSnapshot
            if (showInputSnapshot && registry.all_of<InputSnapshot>(entity)) {
                const auto& c = registry.get<InputSnapshot>(entity);
                ImGui::Text("InputSnapshot  tick: %u", c.tick);
                ImGui::Text("  fwd:%-3s  back:%-3s  left:%-3s  right:%-3s  jump:%-3s  crouch:%-3s",
                            c.forward ? "Y" : "N",
                            c.back ? "Y" : "N",
                            c.left ? "Y" : "N",
                            c.right ? "Y" : "N",
                            c.jump ? "Y" : "N",
                            c.crouch ? "Y" : "N");
            }
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void DebugUI::render()
{
    ImGui::Render();
}
