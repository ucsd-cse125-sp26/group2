/// @file DebugUI.cpp
/// @brief Implementation of the DebugUI overlay and all ImGui debug windows.

#include "debug/DebugUI.hpp"

#include "client/systems/GamepadAimAssistSystem.hpp"
#include "ecs/components/AbilityState.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/RespawnPoint.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/DebugCollisionDraw.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhaseDiagnostic.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/systems/AbilitySystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/Client.hpp" // for NetworkStats
#include "particles/ParticleSystem.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <cfloat>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

// File-local helpers

namespace
{

/// @brief Draw a line with a filled triangular arrowhead at @p end.
/// @param dl        The ImGui draw list to render into.
/// @param start     Line start position in screen coordinates.
/// @param end       Line end position in screen coordinates.
/// @param color     Packed RGBA color.
/// @param thickness Line thickness in pixels.
/// @param headSize  Arrowhead length in pixels.
void drawArrow(ImDrawList* dl, ImVec2 start, ImVec2 end, ImU32 color, float thickness, float headSize)
{
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f)
        return;

    dl->AddLine(start, end, color, thickness);

    // Unit vector along the arrow
    const float ux = dx / len;
    const float uy = dy / len;

    // Arrowhead: tip at `end`, two base corners perpendicular to direction
    dl->AddTriangleFilled(
        end,
        {end.x - ux * headSize - uy * headSize * 0.45f, end.y - uy * headSize + ux * headSize * 0.45f},
        {end.x - ux * headSize + uy * headSize * 0.45f, end.y - uy * headSize - ux * headSize * 0.45f},
        color);
}

const char* weaponTypeName(WeaponType type)
{
    switch (type) {
    case WeaponType::Rifle:
        return "Rifle";
    case WeaponType::Rocket:
        return "Rocket";
    case WeaponType::RailGun:
        return "RailGun";
    case WeaponType::EnergyGun:
        return "EnergyGun";
    case WeaponType::HEGrenade:
        return "HEGrenade";
    case WeaponType::Molotov:
        return "Molotov";
    case WeaponType::Impulse:
        return "Impulse";
    }
    return "Unknown";
}

} // namespace

// DebugUI methods

bool DebugUI::init(SDL_Window* /*window*/)
{
    // ImGui context + SDL3 platform backend are owned by App (created before
    // Renderer::init, destroyed after Renderer::quit).  Nothing to do here.
    return true;
}

void DebugUI::shutdown()
{
    // ImGui context teardown lives in App::cleanup.
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

void DebugUI::toggleDebugMenu()
{
    showDebugMenu = !showDebugMenu;
}

void DebugUI::buildDebugMenu(std::initializer_list<ExternalPanel> externalPanels)
{
    if (!showDebugMenu)
        return;

    ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260.0f, 0.0f}, ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags k_flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Debug Menu (F2)", &showDebugMenu, k_flags)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("F2: toggle this menu  |  F3: toggle cursor");
    ImGui::Separator();

    // DebugUI-owned panels
    ImGui::SeparatorText("Inspector");
    ImGui::Checkbox("ECS Inspector", &showInspector);
    ImGui::Checkbox("Movement Chart", &showMovementChart);
    ImGui::Checkbox("Bhop Analyzer", &showBhopAnalyzer);

    ImGui::SeparatorText("Gameplay");
    ImGui::Checkbox("Network Stats", &showNetworkStats);
    ImGui::Checkbox("Network Sim", &showNetworkSim);
    ImGui::Checkbox("Weapon HUD", &showWeaponHud);
    ImGui::Checkbox("Particle System", &showParticleWindow_);

    ImGui::SeparatorText("Physics");
    ImGui::Checkbox("Hitbox Debug", &showHitboxWindow);
    ImGui::Checkbox("Collision Debug", &showCollisionWindow);
    ImGui::Checkbox("Contact Debug", &showContactDebugWindow);
    ImGui::Checkbox("Weapon Spawners", &showWeaponSpawnerWindow);
    ImGui::Checkbox("Spawn Points", &showSpawnPointWindow);
    ImGui::Checkbox("Shot Debug (sv_showimpacts)", &showShotDebugWindow);

    // External panels (owned by Game)
    if (externalPanels.size() > 0) {
        ImGui::SeparatorText("Game");
        for (const auto& panel : externalPanels) {
            if (panel.visible)
                ImGui::Checkbox(panel.name, panel.visible);
        }
    }

    ImGui::End();
}

void DebugUI::buildUI(const Registry& registry,
                      const int tickCount,
                      float& mouseSensitivity,
                      float& gamepadLookSensitivity,
                      systems::GamepadAimAssistConfig& aimAssistCfg,
                      bool& renderSeparateFromPhysics,
                      bool& inputSyncedWithPhysics,
                      bool& limitFPSToMonitor,
                      const float physicsHz,
                      const float fpsCurrent,
                      const float fpsMin,
                      const float fpsMax,
                      const float fps1pLow,
                      const float fps5pLow)
{
    // The Movement Chart and Bhop Analyzer are children of the inspector's
    // toggle state, but also have their own sub-toggles set from inside this
    // window. They are drawn after End() below.
    if (showInspector) {
        ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({480.0f, 700.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("ECS Inspector", &showInspector)) {
            ImGui::End();
            // Fall through so the sub-windows can still render if their flags say so.
        } else {
            buildInspectorContents(registry,
                                   tickCount,
                                   mouseSensitivity,
                                   gamepadLookSensitivity,
                                   aimAssistCfg,
                                   renderSeparateFromPhysics,
                                   inputSyncedWithPhysics,
                                   limitFPSToMonitor,
                                   physicsHz,
                                   fpsCurrent,
                                   fpsMin,
                                   fpsMax,
                                   fps1pLow,
                                   fps5pLow);
            ImGui::End();
        }
    }

    if (showMovementChart)
        buildMovementChart(registry);

    if (showBhopAnalyzer)
        buildBhopAnalyzer(registry);

    if (showWeaponHud)
        buildWeaponUI(registry);
}

// Contents of the ECS Inspector window, factored out so the Begin/End wrapping
// lives in buildUI() and can be skipped cleanly when the window is hidden.
void DebugUI::buildInspectorContents(const Registry& registry,
                                     const int tickCount,
                                     float& mouseSensitivity,
                                     float& gamepadLookSensitivity,
                                     systems::GamepadAimAssistConfig& aimAssistCfg,
                                     bool& renderSeparateFromPhysics,
                                     bool& inputSyncedWithPhysics,
                                     bool& limitFPSToMonitor,
                                     const float physicsHz,
                                     const float fpsCurrent,
                                     const float fpsMin,
                                     const float fpsMax,
                                     const float fps1pLow,
                                     const float fps5pLow)
{
    // Key bindings reminder
    ImGui::TextDisabled("F2: debug menu  |  F3: toggle cursor  |  ESC: toggle cursor");
    ImGui::Separator();

    // Settings
    ImGui::SeparatorText("Settings");

    // Logarithmic slider so both ends of the range are equally reachable.
    ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity, 0.0001f, 0.0200f, "%.4f", ImGuiSliderFlags_Logarithmic);

    // Gamepad right-stick look speed in radians/second at full deflection.
    // Linear feels right here — the useful range is much narrower than mouse
    // sensitivity (1..15 rad/s covers everything from "console aim assist"
    // to "very twitchy"), and a linear slider preserves intuitive doubling.
    ImGui::SliderFloat("Gamepad Look Sensitivity", &gamepadLookSensitivity, 1.0f, 15.0f, "%.2f rad/s");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Right-stick angular speed at full deflection.\n"
                          "6.0 rad/s ≈ 343°/s (default).\n"
                          "Affects only the gamepad — mouse uses its own slider above.");

    // ── Gamepad aim assist (collapsible) ──────────────────────────────────
    // Tucked behind a header so the inspector stays clean for kbm players.
    // All sliders live-edit the active config struct on the Game class, so
    // tuning is immediate.  Default-open while we iterate on the feel.
    if (ImGui::CollapsingHeader("Gamepad Aim Assist", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &aimAssistCfg.enabled);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Master toggle.  No effect when no gamepad is connected.");

        ImGui::BeginDisabled(!aimAssistCfg.enabled);

        ImGui::SliderFloat("Inner Cone (deg)", &aimAssistCfg.innerConeDeg, 0.0f, 15.0f, "%.1f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Crosshair-to-target angle below which assist is at full strength.");

        ImGui::SliderFloat("Outer Cone (deg)", &aimAssistCfg.outerConeDeg, 0.0f, 30.0f, "%.1f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Angle at which assist fades to zero.\n"
                              "Larger = bigger \"magnet\" but more obvious.");

        ImGui::SliderFloat("Max Range (units)", &aimAssistCfg.maxRange, 200.0f, 8000.0f, "%.0f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Targets beyond this distance get no aim assist.");

        ImGui::SliderFloat("Activation Stick %", &aimAssistCfg.activationStickThresh, 0.0f, 0.30f, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Stick deflection (left or right) required to activate assist.\n"
                              "0.05 = 5 %% (default).  Holding still disables all assist.");

        ImGui::SliderFloat("Rotational Compensation", &aimAssistCfg.rotationalCompensation, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Fraction of the target's apparent angular velocity that aim\n"
                              "assist contributes per frame.  0.0 = no rotational help,\n"
                              "1.0 = perfect tracking (aimbot).  Default 0.8.\n"
                              "A stationary enemy contributes ZERO regardless of this value —\n"
                              "the pull is driven by frame-to-frame change in the anchor\n"
                              "position, not by absolute angle to the target.");

        ImGui::SliderFloat("Max Pull Rate (rad/s)", &aimAssistCfg.maxPullRate, 0.5f, 8.0f, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Hard cap on how fast the camera can be pulled by assist.\n"
                              "Safety net for snapshot warps / target teleports.\n"
                              "3.0 rad/s ≈ 172°/s (default).");

        ImGui::SliderFloat("Slowdown Strength", &aimAssistCfg.slowdownStrength, 0.10f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Effective look sensitivity when crosshair is on a target.\n"
                              "Lower = stronger slowdown / stickier feel.\n"
                              "1.0 = no slowdown, 0.35 = 35 %% speed (default).");

        ImGui::EndDisabled();
    }

    ImGui::Checkbox("Render Separately from Physics", &renderSeparateFromPhysics);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("ON:  render every frame with position interpolated between ticks\n"
                          "OFF: render only after a physics tick (fps capped at 128)");

    ImGui::Checkbox("Input Synced w/ Physics", &inputSyncedWithPhysics);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("ON:  movement keys (WASD) sampled once per tick (server-consistent)\n"
                          "OFF: movement keys sampled every frame\n"
                          "Mouse look is always per-frame regardless of this toggle");

    ImGui::Checkbox("Limit FPS to Monitor Refresh", &limitFPSToMonitor);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("ON + monitor >= physics Hz: VSync (locks to monitor refresh)\n"
                          "ON + monitor <  physics Hz: software limiter at physics Hz\n"
                          "OFF + monitor >= physics Hz: uncapped (mailbox present)\n"
                          "OFF + monitor <  physics Hz: software limiter at physics Hz\n"
                          "  (sub-physics-Hz monitors always cap at physics Hz for\n"
                          "   smooth frame pacing — the monitor can't display faster)");

    ImGui::Separator();

    // Performance
    ImGui::SeparatorText("Performance");
    ImGui::Text("Phys: %5.1f Hz    Tick: %d", static_cast<double>(physicsHz), tickCount);
    ImGui::Text("FPS  cur:%5.0f  1%%:%5.0f  5%%:%5.0f  min:%5.0f  max:%5.0f",
                static_cast<double>(fpsCurrent),
                static_cast<double>(fps1pLow),
                static_cast<double>(fps5pLow),
                static_cast<double>(fpsMin),
                static_cast<double>(fpsMax));

    const auto* const k_entityStorage = registry.storage<entt::entity>();

    int entityCount = 0;
    if (k_entityStorage)
        for (const auto entity : *k_entityStorage)
            if (registry.valid(entity))
                ++entityCount;

    ImGui::Text("Entities: %d", entityCount);
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
    ImGui::SameLine();
    ImGui::Checkbox("Movement Chart", &showMovementChart);

    if (!k_entityStorage)
        return;

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
            if (showPlayerState && registry.all_of<PlayerVisState>(entity)) {
                const auto& c = registry.get<PlayerVisState>(entity);
                static const char* k_modeNames[] = {"OnFoot", "Sliding", "WallRun", "Climbing", "LedgeGrab"};
                const int k_modeIdx = static_cast<int>(c.moveMode);
                ImGui::Text("PlayerState   mode:%s  grounded:%-3s  crouching:%-3s  sprint:%-3s",
                            (k_modeIdx >= 0 && k_modeIdx < 5) ? k_modeNames[k_modeIdx] : "?",
                            c.grounded ? "YES" : "NO",
                            c.crouching ? "YES" : "NO",
                            c.sprinting ? "YES" : "NO");
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
}

void DebugUI::buildMovementChart(const Registry& registry)
{
    // Find local player
    entt::entity localPlayer = entt::null;
    const auto* const k_es = registry.storage<entt::entity>();
    if (k_es) {
        for (const auto e : *k_es) {
            if (registry.valid(e) && registry.all_of<LocalPlayer>(e)) {
                localPlayer = e;
                break;
            }
        }
    }

    // Window setup
    ImGui::SetNextWindowPos({500.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({430.0f, 470.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Movement Chart", &showMovementChart)) {
        ImGui::End();
        return;
    }

    // Canvas geometry
    // Reserve a square canvas; keep ~52 px below it for the stats line + legend.
    const ImVec2 k_cursor = ImGui::GetCursorScreenPos();
    const ImVec2 k_avail = ImGui::GetContentRegionAvail();
    const float k_side = std::max(std::min(k_avail.x, k_avail.y - 52.0f), 80.0f);
    const ImVec2 k_canvasP1 = {k_cursor.x + k_side, k_cursor.y + k_side};

    // The invisible button "consumes" the canvas area so ImGui lays out correctly.
    ImGui::InvisibleButton("##mvmap", {k_side, k_side});
    ImDrawList* const dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(k_cursor, k_canvasP1, true);

    // Coordinate helpers
    // World space:  ±1 500 units in X and Z, centred at the spawn origin.
    // Chart axes:   world -X  → screen right  (camera right = world -X)
    //               world +Z  → screen up     (flip, because screen Y grows downward)
    constexpr float k_worldHalf = 1500.0f;
    const float k_posScale = k_side / (2.0f * k_worldHalf); // px per world unit

    // Velocity vectors use a separate scale so they're always clearly visible:
    // max ground wish speed (sprint) maps to 28 % of the canvas half-width.
    const float k_velScale = (k_side * 0.28f) / tms::k_sprintSpeed;

    const auto worldToScreen = [&](float wx, float wz) -> ImVec2 {
        return {k_cursor.x + k_side * 0.5f - wx * k_posScale, // negate X: world -X = screen right
                k_cursor.y + k_side * 0.5f - wz * k_posScale};
    };

    // Background
    dl->AddRectFilled(k_cursor, k_canvasP1, IM_COL32(14, 14, 22, 255));

    // Grid lines (every 500 world units)
    for (int i = -2; i <= 2; ++i) {
        const float w = static_cast<float>(i) * 500.0f;
        const bool isCtr = (i == 0);
        const ImU32 col = isCtr ? IM_COL32(65, 65, 95, 255) : IM_COL32(40, 40, 60, 255);
        const float th = isCtr ? 1.5f : 1.0f;
        dl->AddLine(worldToScreen(w, -k_worldHalf), worldToScreen(w, k_worldHalf), col, th);
        dl->AddLine(worldToScreen(-k_worldHalf, w), worldToScreen(k_worldHalf, w), col, th);
    }

    // Axis labels at the edge of the canvas
    const auto labelOffset = 14.0f;
    dl->AddText(worldToScreen(-4.0f, k_worldHalf - labelOffset), IM_COL32(110, 110, 155, 200), "+Z");
    dl->AddText(worldToScreen(k_worldHalf - labelOffset - 4.0f, -4.0f), IM_COL32(110, 110, 155, 200), "-X");

    // Small origin marker
    const ImVec2 k_orig = worldToScreen(0.0f, 0.0f);
    dl->AddCircle(k_orig, 3.0f, IM_COL32(90, 90, 130, 200), 8, 1.0f);

    // Player
    const bool k_hasPlayer =
        localPlayer != entt::null && registry.all_of<Position, Velocity, InputSnapshot, PlayerVisState>(localPlayer);

    if (k_hasPlayer) {
        const auto& pos = registry.get<Position>(localPlayer).value;
        const auto& vel = registry.get<Velocity>(localPlayer).value;
        const auto& input = registry.get<InputSnapshot>(localPlayer);
        const auto& playerState = registry.get<PlayerVisState>(localPlayer);
        const bool grounded = playerState.grounded;

        const ImVec2 k_pScreen = worldToScreen(pos.x, pos.z);

        // 1. View-direction arrow — yaw only, fixed screen length so it's always readable.
        //    Forward in world space at yaw θ:  dir = (sin θ,  0,  cos θ)
        constexpr float k_viewScreenLen = 72.0f; // px
        {
            const float sy = std::sin(input.yaw);
            const float cy = std::cos(input.yaw);
            drawArrow(
                dl,
                k_pScreen,
                {k_pScreen.x - sy * k_viewScreenLen, k_pScreen.y - cy * k_viewScreenLen}, // -X: world -X = screen right
                IM_COL32(255, 225, 70, 215),
                2.0f,
                9.0f);
        }

        // 2. Wish-velocity arrow — direction + magnitude relative to velocity scale.
        {
            const glm::vec3 wishDir =
                physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
            const float wishSpeed = grounded ? systems::currentWishSpeed(playerState) : physics::k_airMaxSpeed;
            drawArrow(
                dl,
                k_pScreen,
                {k_pScreen.x - wishDir.x * wishSpeed * k_velScale, k_pScreen.y - wishDir.z * wishSpeed * k_velScale},
                IM_COL32(70, 255, 130, 215),
                2.0f,
                8.0f);
        }

        // 3. Velocity arrow — actual XZ velocity at the same velocity scale.
        drawArrow(dl,
                  k_pScreen,
                  {k_pScreen.x - vel.x * k_velScale, k_pScreen.y - vel.z * k_velScale},
                  IM_COL32(75, 175, 255, 215),
                  2.5f,
                  10.0f);

        // Player dot (drawn last so it sits on top of all arrows)
        auto playerColor = grounded ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
        dl->AddCircleFilled(k_pScreen, 5.0f, playerColor);
        dl->AddCircle(k_pScreen, 5.5f, IM_COL32(0, 0, 0, 180), 12, 1.5f);
    } else {
        const char* k_msg = "No local player";
        dl->AddText({k_cursor.x + k_side * 0.5f - 50.0f, k_cursor.y + k_side * 0.5f - 7.0f},
                    IM_COL32(140, 140, 140, 200),
                    k_msg);
    }

    // Canvas border
    dl->AddRect(k_cursor, k_canvasP1, IM_COL32(75, 75, 115, 255), 0.0f, 0, 1.5f);
    dl->PopClipRect();

    // Stats line
    ImGui::Spacing();
    if (k_hasPlayer) {
        const auto& vel = registry.get<Velocity>(localPlayer).value;
        const auto& input = registry.get<InputSnapshot>(localPlayer);
        const auto& playerState = registry.get<PlayerVisState>(localPlayer);
        const bool grounded = playerState.grounded;
        const float hSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        const glm::vec3 wishDir =
            physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
        const float wishSpeed = grounded ? systems::currentWishSpeed(playerState) : physics::k_airMaxSpeed;
        const float wishMag = (wishDir.x != 0.0f || wishDir.z != 0.0f) ? wishSpeed : 0.0f;

        ImGui::Text("XZ: %5.0f  |  Y: %+6.1f  |  Wish: %5.0f  |  %s",
                    static_cast<double>(hSpeed),
                    static_cast<double>(vel.y),
                    static_cast<double>(wishMag),
                    grounded ? "GROUND" : "AIR");
    } else {
        ImGui::TextDisabled("--");
    }

    // Legend
    ImGui::Spacing();
    {
        ImDrawList* const ldl = ImGui::GetWindowDrawList();
        const auto legendSwatch = [&](const char* label, ImU32 color) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy({12.0f, 12.0f});
            ldl->AddRectFilled({p.x + 1, p.y + 3}, {p.x + 11, p.y + 11}, color);
            ImGui::SameLine(0.0f, 5.0f);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(0.0f, 18.0f);
        };
        legendSwatch("View dir", IM_COL32(255, 225, 70, 215));
        legendSwatch("Velocity", IM_COL32(75, 175, 255, 215));
        legendSwatch("Wish vel", IM_COL32(70, 255, 130, 215));
        ImGui::NewLine();
    }

    ImGui::End();
}

// Bhop Analyzer — player-relative vectors, gain, sync, history plots.

void DebugUI::buildBhopAnalyzer(const Registry& registry)
{
    // Find the local player entity (same pattern as buildMovementChart).
    entt::entity localPlayer = entt::null;
    const auto* const k_es = registry.storage<entt::entity>();
    if (k_es) {
        for (auto e : *k_es) {
            if (registry.all_of<LocalPlayer>(e)) {
                localPlayer = e;
                break;
            }
        }
    }

    ImGui::SetNextWindowPos({940.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({440.0f, 600.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bhop Analyzer", &showBhopAnalyzer)) {
        ImGui::End();
        return;
    }

    const bool k_hasPlayer =
        localPlayer != entt::null && registry.all_of<Position, Velocity, InputSnapshot, PlayerVisState>(localPlayer);

    if (!k_hasPlayer) {
        ImGui::TextDisabled("No local player.");
        ImGui::End();
        return;
    }

    const auto& vel = registry.get<Velocity>(localPlayer).value;
    const auto& input = registry.get<InputSnapshot>(localPlayer);
    const auto& playerState = registry.get<PlayerVisState>(localPlayer);
    const bool grounded = playerState.grounded;
    const float yaw = input.yaw;

    // Horizontal-speed scalar (what bhop watches).
    const float k_hSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);

    // Gain = Δ horizontal speed since previous frame. First sample has no baseline.
    const float k_gain = bhopHasPrevSample_ ? (k_hSpeed - bhopPrevHSpeed_) : 0.0f;
    const bool k_hasInput = input.forward || input.back || input.left || input.right;

    // Push this frame into the ring buffers.
    bhopSpeedHistory_[bhopHistoryIdx_] = k_hSpeed;
    bhopGainHistory_[bhopHistoryIdx_] = k_gain;
    bhopAirborneHistory_[bhopHistoryIdx_] = !grounded && k_hasInput;
    // A "gaining" frame: airborne, has input, speed went up. The classic bhop sync definition.
    bhopGainingHistory_[bhopHistoryIdx_] = bhopAirborneHistory_[bhopHistoryIdx_] && k_gain > 0.0f;
    bhopHistoryIdx_ = (bhopHistoryIdx_ + 1) % k_bhopHistorySize;
    if (bhopHistoryFill_ < k_bhopHistorySize)
        ++bhopHistoryFill_;
    bhopPrevHSpeed_ = k_hSpeed;
    bhopHasPrevSample_ = true;

    // Rotate world velocity / wishdir into player-local frame.
    // Player forward at yaw θ in world = (sin θ, 0, cos θ). Rotating by −θ around Y
    // maps player forward → +Z (world), which our draw convention puts at screen-up.
    const float k_sinY = std::sin(yaw);
    const float k_cosY = std::cos(yaw);
    const auto toLocal = [&](float wx, float wz) -> glm::vec2 {
        // R(−yaw) * (wx, wz): localX = wx*cos − wz*sin,  localZ = wx*sin + wz*cos
        return {wx * k_cosY - wz * k_sinY, wx * k_sinY + wz * k_cosY};
    };

    const glm::vec2 k_velLocal = toLocal(vel.x, vel.z); // right-axis (parallel to strafe), forward-axis
    const glm::vec3 k_wishDirWorld =
        physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
    const glm::vec2 k_wishLocal = toLocal(k_wishDirWorld.x, k_wishDirWorld.z);

    // Sync % over the full window, restricted to airborne+input frames.
    int airborneCount = 0;
    int gainingCount = 0;
    for (int i = 0; i < bhopHistoryFill_; ++i) {
        if (bhopAirborneHistory_[i])
            ++airborneCount;
        if (bhopGainingHistory_[i])
            ++gainingCount;
    }
    const float k_syncPct =
        (airborneCount > 0) ? (100.0f * static_cast<float>(gainingCount) / static_cast<float>(airborneCount)) : 0.0f;

    // Canvas
    ImGui::SeparatorText("Player-relative vectors");
    const ImVec2 k_cursor = ImGui::GetCursorScreenPos();
    const ImVec2 k_avail = ImGui::GetContentRegionAvail();
    const float k_side = std::max(std::min(k_avail.x, 260.0f), 80.0f);
    const ImVec2 k_canvasP1 = {k_cursor.x + k_side, k_cursor.y + k_side};
    ImGui::InvisibleButton("##bhopmap", {k_side, k_side});
    ImDrawList* const dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(k_cursor, k_canvasP1, true);

    // Background + axes
    dl->AddRectFilled(k_cursor, k_canvasP1, IM_COL32(14, 14, 22, 255));
    const ImVec2 k_center = {k_cursor.x + k_side * 0.5f, k_cursor.y + k_side * 0.5f};
    dl->AddLine({k_cursor.x, k_center.y}, {k_canvasP1.x, k_center.y}, IM_COL32(50, 50, 75, 255), 1.0f);
    dl->AddLine({k_center.x, k_cursor.y}, {k_center.x, k_canvasP1.y}, IM_COL32(50, 50, 75, 255), 1.0f);
    dl->AddText({k_center.x + 4, k_cursor.y + 2}, IM_COL32(110, 110, 155, 200), "Fwd");
    dl->AddText({k_cursor.x + 4, k_center.y + 2}, IM_COL32(110, 110, 155, 200), "Right");

    // Reference scale: sprint speed → 28% of half-canvas (matches MovementChart convention).
    const float k_velScale = (k_side * 0.28f) / tms::k_sprintSpeed;

    // 1. Fixed view arrow (always straight up, since this is the player's frame).
    constexpr float k_viewLen = 48.0f;
    drawArrow(dl, k_center, {k_center.x, k_center.y - k_viewLen}, IM_COL32(255, 225, 70, 215), 2.0f, 9.0f);

    // 2. Wish-velocity arrow. In this panel we use raw wishDir (no speed scaling) times a fixed length
    //    so the arrow direction is always readable regardless of current wish speed.
    if (glm::length(glm::vec2(k_wishLocal.x, k_wishLocal.y)) > 0.001f) {
        constexpr float k_wishLen = 60.0f;
        // localX = right-component (world -X was screen right → flip sign for screen).
        // localZ = forward-component (world +Z was screen up → flip sign for screen).
        drawArrow(dl,
                  k_center,
                  {k_center.x - k_wishLocal.x * k_wishLen, k_center.y - k_wishLocal.y * k_wishLen},
                  IM_COL32(70, 255, 130, 215),
                  2.0f,
                  8.0f);
    }

    // 3. Velocity arrow, scaled to sprint-speed reference.
    drawArrow(dl,
              k_center,
              {k_center.x - k_velLocal.x * k_velScale, k_center.y - k_velLocal.y * k_velScale},
              IM_COL32(75, 175, 255, 215),
              2.5f,
              10.0f);

    // Player dot on top
    const ImU32 k_dotColor = grounded ? IM_COL32(255, 80, 80, 255) : IM_COL32(255, 255, 255, 255);
    dl->AddCircleFilled(k_center, 4.5f, k_dotColor);
    dl->AddCircle(k_center, 5.0f, IM_COL32(0, 0, 0, 180), 12, 1.5f);

    dl->PopClipRect();
    dl->AddRect(k_cursor, k_canvasP1, IM_COL32(75, 75, 115, 255), 0.0f, 0, 1.5f);

    // Stats
    ImGui::Spacing();
    ImGui::SeparatorText("Vectors");
    ImGui::Text("Velocity (local):  fwd=%+7.1f  right=%+7.1f  up=%+7.1f",
                static_cast<double>(k_velLocal.y),
                static_cast<double>(k_velLocal.x),
                static_cast<double>(vel.y));
    ImGui::Text("Wish dir (local):  fwd=%+5.2f  right=%+5.2f",
                static_cast<double>(k_wishLocal.y),
                static_cast<double>(k_wishLocal.x));
    ImGui::Text("|XZ speed|:        %.1f u/s", static_cast<double>(k_hSpeed));
    ImGui::Text("State:             %s", grounded ? "GROUND" : "AIR");

    ImGui::SeparatorText("Bhop metrics");
    const ImU32 gainColor = (k_gain > 0.5f)
                                ? IM_COL32(80, 230, 120, 255)
                                : (k_gain < -0.5f ? IM_COL32(235, 90, 90, 255) : IM_COL32(180, 180, 180, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, gainColor);
    ImGui::Text("Gain (this frame): %+6.2f u/s", static_cast<double>(k_gain));
    ImGui::PopStyleColor();
    ImGui::Text("Sync:              %5.1f %%   (%d / %d airborne-input frames)",
                static_cast<double>(k_syncPct),
                gainingCount,
                airborneCount);

    // Plots — assembled in chronological order so the graph scrolls left-to-right.
    ImGui::SeparatorText("History");
    float speedOrdered[k_bhopHistorySize];
    float gainOrdered[k_bhopHistorySize];
    const int k_count = bhopHistoryFill_;
    // Ring start depends on whether we've wrapped.
    const int k_start = (bhopHistoryFill_ < k_bhopHistorySize) ? 0 : bhopHistoryIdx_;
    for (int i = 0; i < k_count; ++i) {
        const int src = (k_start + i) % k_bhopHistorySize;
        speedOrdered[i] = bhopSpeedHistory_[src];
        gainOrdered[i] = bhopGainHistory_[src];
    }

    ImGui::PlotLines("XZ speed", speedOrdered, k_count, 0, nullptr, 0.0f, FLT_MAX, {-1.0f, 70.0f});
    // For gain, force a symmetric y-range around 0 so losses and gains read at the same scale.
    float gainAbsMax = 1.0f;
    for (int i = 0; i < k_count; ++i)
        gainAbsMax = std::max(gainAbsMax, std::abs(gainOrdered[i]));
    ImGui::PlotLines("Gain", gainOrdered, k_count, 0, nullptr, -gainAbsMax, gainAbsMax, {-1.0f, 70.0f});

    // Legend
    ImGui::Spacing();
    {
        ImDrawList* const ldl = ImGui::GetWindowDrawList();
        const auto legendSwatch = [&](const char* label, ImU32 color) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy({12.0f, 12.0f});
            ldl->AddRectFilled({p.x + 1, p.y + 3}, {p.x + 11, p.y + 11}, color);
            ImGui::SameLine(0.0f, 5.0f);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(0.0f, 18.0f);
        };
        legendSwatch("View", IM_COL32(255, 225, 70, 215));
        legendSwatch("Velocity", IM_COL32(75, 175, 255, 215));
        legendSwatch("Wish", IM_COL32(70, 255, 130, 215));
        ImGui::NewLine();
    }

    ImGui::End();
}

// Particle System debug/control window

void DebugUI::buildParticleUI(ParticleSystem& ps, glm::vec3 eyePos, glm::vec3 forward)
{
    if (!showParticleWindow_)
        return;

    ImGui::SetNextWindowPos({10.f, 620.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({440.f, 520.f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Particle System", &showParticleWindow_)) {
        ImGui::End();
        return;
    }

    // Status
    ImGui::SeparatorText("Status");
    if (ps.sdfReady())
        ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f}, "SDF Font: LOADED");
    else
        ImGui::TextColored({1.f, 0.5f, 0.3f, 1.f}, "SDF Font: not loaded (text rendering disabled)");

    // Live counts
    ImGui::SeparatorText("Live Counts");

    struct PoolRow
    {
        const char* name;
        uint32_t live;
        uint32_t maxN;
    };
    const PoolRow rows[] = {
        {"Sparks / Impact", ps.impactCount(), 4096},
        {"Tracers (caps.)", ps.tracerCount(), 512},
        {"Ribbon verts", ps.ribbonVertexCount(), 24576},
        {"Hitscan beams", ps.hitscanBeamCount(), 64},
        {"Arc verts", ps.arcVertexCount(), 2048},
        {"Smoke", ps.smokeCount(), 1024},
        {"Decals", ps.decalCount(), 512},
    };

    constexpr ImGuiTableFlags kTF =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##counts", 3, kTF)) {
        ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthFixed, 150.f);
        ImGui::TableSetupColumn("Live/Max", ImGuiTableColumnFlags_WidthFixed, 88.f);
        ImGui::TableSetupColumn("Fill", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(r.name);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u / %u", r.live, r.maxN);
            ImGui::TableSetColumnIndex(2);
            const float fraction = (r.maxN > 0) ? static_cast<float>(r.live) / static_cast<float>(r.maxN) : 0.f;
            char overlay[16];
            SDL_snprintf(overlay, sizeof(overlay), "%.0f%%", static_cast<double>(fraction * 100.f));
            ImGui::ProgressBar(fraction, {-FLT_MIN, 0.f}, overlay);
        }
        ImGui::EndTable();
    }

    // Spawn Controls
    ImGui::SeparatorText("Spawn Controls");

    ImGui::SliderFloat("Dist ahead (units)", &particleSpawnDist_, 30.f, 800.f, "%.0f");

    const glm::vec3 worldUp = {0.f, 1.f, 0.f};
    const glm::vec3 camRight = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 hipfireOrigin = eyePos + camRight * 15.f - worldUp * 8.f + forward * 5.f;
    const glm::vec3 spawnPos = eyePos + forward * particleSpawnDist_;
    const glm::vec3 wallNorm = -forward;

    ImGui::Spacing();

    // Weapon effects
    if (ImGui::Button("Shoot Bullet (R301)", {160.f, 0.f})) {
        ps.spawnBulletTracer(hipfireOrigin, forward, particleSpawnDist_);
        ps.spawnImpactEffect(
            hipfireOrigin + forward * particleSpawnDist_, wallNorm, SurfaceType::Metal, WeaponType::Rifle);
    }
    ImGui::SameLine();
    if (ImGui::Button("Energy Shot", {110.f, 0.f})) {
        const glm::vec3 hitPoint = hipfireOrigin + forward * particleSpawnDist_;
        ps.spawnHitscanBeam(hipfireOrigin, hitPoint, WeaponType::EnergyGun);
        ps.spawnImpactEffect(hitPoint, wallNorm, SurfaceType::Energy, WeaponType::EnergyGun);
    }

    if (ImGui::Button("Smoke Cloud", {120.f, 0.f}))
        ps.spawnSmoke(spawnPos, 40.f);
    ImGui::SameLine();
    if (ImGui::Button("Explosion", {100.f, 0.f}))
        ps.spawnExplosion(spawnPos, 100.f);

    ImGui::Spacing();
    ImGui::SeparatorText("Keyboard Shortcuts");
    ImGui::TextDisabled("T: Hitscan beam    Y: Metal impact");
    ImGui::TextDisabled("U: Smoke cloud     I: Explosion");
    ImGui::TextDisabled("Left-click: Fire weapon");

    ImGui::End();
}

void DebugUI::buildNetworkUI(const NetworkStats& stats)
{
    if (!showNetworkStats)
        return;

    ImGui::SetNextWindowPos({500.0f, 490.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({300.0f, 200.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network Stats", &showNetworkStats)) {
        ImGui::End();
        return;
    }

    // Ping / RTT
    ImGui::SeparatorText("Latency");
    ImGui::Text("Ping:      %5.1f ms", static_cast<double>(stats.rttMs));
    ImGui::Text("Avg Ping:  %5.1f ms", static_cast<double>(stats.avgRttMs));

    // Bandwidth
    ImGui::SeparatorText("Bandwidth");
    const float recvMBs = stats.recvBytesPerSec / (1024.0f * 1024.0f);
    const float sendMBs = stats.sendBytesPerSec / (1024.0f * 1024.0f);
    ImGui::Text("Recv: %6.3f MB/s  (%.0f KB/s)",
                static_cast<double>(recvMBs),
                static_cast<double>(stats.recvBytesPerSec / 1024.0f));
    ImGui::Text("Send: %6.3f MB/s  (%.0f KB/s)",
                static_cast<double>(sendMBs),
                static_cast<double>(stats.sendBytesPerSec / 1024.0f));

    // Registry updates
    ImGui::SeparatorText("Registry");
    ImGui::Text("Update size:  %5.1f KB", static_cast<double>(stats.registryUpdateSize) / 1024.0);
    ImGui::Text("Updates/sec:  %5.1f", static_cast<double>(stats.registryUpdatesPerSec));

    // Totals
    ImGui::SeparatorText("Totals");
    ImGui::Text("Recv: %.2f MB   Send: %.2f MB",
                static_cast<double>(stats.bytesRecvTotal) / (1024.0 * 1024.0),
                static_cast<double>(stats.bytesSentTotal) / (1024.0 * 1024.0));

    ImGui::End();
}

void DebugUI::buildNetworkSimUI()
{
    if (!showNetworkSim)
        return;

    ImGui::SetNextWindowPos({500.0f, 700.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags k_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Network Sim", &showNetworkSim, k_flags)) {
        ImGui::End();
        return;
    }

    // ── Latency ────────────────────────────────────────────────────────
    ImGui::SeparatorText("Latency");
    ImGui::TextWrapped("Adds artificial round-trip latency. Half is applied to "
                       "outbound, half to inbound, modelling a symmetric one-way delay.");
    ImGui::Spacing();
    ImGui::SliderInt("Sim. RTT (ms)", &simulatedLatencyMs_, 0, 200, "%d ms");

    // Quick presets — common latency tiers for testing lag-comp /
    // reconciliation behaviour at "LAN", "good WAN", "bad WAN", "edge".
    if (ImGui::SmallButton("0##lat"))
        simulatedLatencyMs_ = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("30##lat"))
        simulatedLatencyMs_ = 30;
    ImGui::SameLine();
    if (ImGui::SmallButton("60##lat"))
        simulatedLatencyMs_ = 60;
    ImGui::SameLine();
    if (ImGui::SmallButton("100##lat"))
        simulatedLatencyMs_ = 100;
    ImGui::SameLine();
    if (ImGui::SmallButton("150##lat"))
        simulatedLatencyMs_ = 150;
    ImGui::SameLine();
    if (ImGui::SmallButton("200##lat"))
        simulatedLatencyMs_ = 200;

    if (simulatedLatencyMs_ == 0) {
        ImGui::TextDisabled("Latency: off — packets take the fast path.");
    } else {
        ImGui::Text(
            "Out %d ms / In %d ms / RTT +%d ms", simulatedLatencyMs_ / 2, simulatedLatencyMs_ / 2, simulatedLatencyMs_);
    }

    // ── Packet loss ────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Packet Loss");
    ImGui::TextWrapped("Independent Bernoulli drop on each direction. Slider value "
                       "is per-datagram drop probability — fragmented snapshots and "
                       "redundant inputs/events lose effective bandwidth at the "
                       "compounded rate.");
    ImGui::Spacing();
    ImGui::SliderInt("Sim. Loss (%)", &simulatedLossPct_, 0, 50, "%d %%");

    if (ImGui::SmallButton("0##loss"))
        simulatedLossPct_ = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("1##loss"))
        simulatedLossPct_ = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("5##loss"))
        simulatedLossPct_ = 5;
    ImGui::SameLine();
    if (ImGui::SmallButton("10##loss"))
        simulatedLossPct_ = 10;
    ImGui::SameLine();
    if (ImGui::SmallButton("25##loss"))
        simulatedLossPct_ = 25;
    ImGui::SameLine();
    if (ImGui::SmallButton("50##loss"))
        simulatedLossPct_ = 50;

    if (simulatedLossPct_ == 0) {
        ImGui::TextDisabled("Loss: off — every UDP datagram delivered.");
    } else {
        // Compounded effective loss across redundancy layers, for
        // playtester intuition.  Closed-form for an N-fragment / N-copy
        // unit: 1 − (1 − p)^N if a single drop kills it (snapshots), or
        // p^N if all copies must be dropped (reliable events).
        const double p = static_cast<double>(simulatedLossPct_) / 100.0;
        const double snapLoss = 1.0 - std::pow(1.0 - p, 5.0); // 5-fragment example
        const double reliable = std::pow(p, 3.0);             // 3-copy redundancy
        const double inputs = std::pow(p, 5.0);               // 5-tick redundancy
        ImGui::Text("Per-datagram drop: %d %%", simulatedLossPct_);
        ImGui::Text("≈ snapshot loss (5 frags): %.1f %%", snapLoss * 100.0);
        ImGui::Text("≈ reliable-event loss:    %.2f %%", reliable * 100.0);
        ImGui::Text("≈ input-tick loss:        %.3f %%", inputs * 100.0);
    }

    ImGui::End();
}

void DebugUI::buildWeaponUI(const Registry& registry)
{
    entt::entity localPlayer = entt::null;
    const auto* const k_es = registry.storage<entt::entity>();
    if (k_es) {
        for (auto e : *k_es) {
            if (registry.valid(e) && registry.all_of<LocalPlayer>(e)) {
                localPlayer = e;
                break;
            }
        }
    }

    ImGui::SetNextWindowPos({10.0f, 540.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260.0f, 220.0f}, ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags k_flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Weapon HUD", nullptr, k_flags)) {
        ImGui::End();
        return;
    }

    if (localPlayer == entt::null) {
        ImGui::TextDisabled("Local player not available");
        ImGui::End();
        return;
    }

    if (!registry.all_of<WeaponState>(localPlayer)) {
        ImGui::TextDisabled("Weapon state unavailable");
        ImGui::End();
        return;
    }

    const WeaponState& weapon = registry.get<WeaponState>(localPlayer);
    const GunInstance& gun = getEquippedGun(weapon);

    const char* currentGunName = weaponTypeName(gun.type);

    ImGui::SeparatorText("Weapon");
    ImGui::Text("Current: %s", currentGunName);
    ImGui::Text("Ammo:    %d / %d", gun.currentMagAmmo, gun.totalAmmo);

    // Flag checked by Game::iterate() to refill ammo (registry is const here).
    if (ImGui::Button("Refill All Ammo"))
        pendingAmmoRefill_ = true;

    ImGui::SeparatorText("Abilities");
    if (const auto* ability = registry.try_get<AbilityState>(localPlayer)) {
        const char* pendingSlot = "None";
        if (ability->pendingLevel1) {
            pendingSlot = "Primary";
        } else if (ability->pendingLevel2) {
            pendingSlot = "Secondary";
        }

        ImGui::Text("Level:   %d", ability->level);
        ImGui::Text("Damage:  %.0f / %.0f",
                    static_cast<double>(ability->accumDamage),
                    static_cast<double>(systems::dmgThreshold));
        ImGui::Text("Primary: %s", abilityName(ability->primary));
        ImGui::Text("Second:  %s", abilityName(ability->secondary));
        ImGui::Text("Pending: %s", pendingSlot);
    } else {
        ImGui::TextDisabled("Ability state unavailable");
    }

    if (ImGui::Button("Grant Ability Level"))
        pendingAbilityLevelGrant_ = true;

    ImGui::SeparatorText("Vitals");
    if (registry.all_of<Health>(localPlayer)) {
        const Health& health = registry.get<Health>(localPlayer);
        ImGui::Text("Overshield: %.0f", static_cast<double>(health.overShield));
        ImGui::Text("Armor:   %.0f / %.0f", static_cast<double>(health.armor), static_cast<double>(systems::armorMax));
        ImGui::Text(
            "Health:  %.0f / %.0f", static_cast<double>(health.health), static_cast<double>(systems::healthMax));
    } else {
        ImGui::TextDisabled("Health state unavailable");
    }

    ImGui::End();
}

// Hitbox capsule debug visualization

namespace
{

/// @brief Project a world-space point to screen-space using the view-projection matrix.
/// Returns false if the point is behind the camera.
bool worldToScreen(glm::vec3 world, const glm::mat4& vp, float w, float h, ImVec2& out)
{
    const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f)
        return false; // behind camera
    const float invW = 1.0f / clip.w;
    out.x = (0.5f + 0.5f * clip.x * invW) * w;
    out.y = (0.5f - 0.5f * clip.y * invW) * h;
    return true;
}

/// @brief Draw a screen-space line between two world points if both are visible.
void drawWorldLine(ImDrawList* dl,
                   glm::vec3 a,
                   glm::vec3 b,
                   const glm::mat4& vp,
                   float sw,
                   float sh,
                   ImU32 color,
                   float thickness = 1.0f)
{
    ImVec2 sa, sb;
    if (worldToScreen(a, vp, sw, sh, sa) && worldToScreen(b, vp, sw, sh, sb))
        dl->AddLine(sa, sb, color, thickness);
}

/// @brief Draw a capsule wireframe (two hemispheres connected by 4 lines).
///
/// Renders equator rings at pA and pB, four longitudinal struts, and
/// hemisphere arcs on each end so the shape reads as a pill, not a cylinder.
void drawCapsuleWireframe(
    ImDrawList* dl, glm::vec3 pA, glm::vec3 pB, float radius, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    // Capsule axis
    glm::vec3 axis = pB - pA;
    const float axisLen = glm::length(axis);
    if (axisLen < 0.001f) {
        // Degenerate — draw a circle.
        ImVec2 center;
        if (!worldToScreen(pA, vp, sw, sh, center))
            return;
        dl->AddCircle(center, 5.0f, color, 12, 1.5f);
        return;
    }
    axis /= axisLen;

    // Build perpendicular vectors.
    glm::vec3 up = (std::abs(axis.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(axis, up));
    up = glm::normalize(glm::cross(right, axis));

    constexpr int ringSegments = 12;
    constexpr int arcSegments = 8;
    constexpr float pi2 = 6.2831853f;
    constexpr float halfPi = 1.5707963f;

    // Draw equator rings at both endpoints.
    for (int endIdx = 0; endIdx < 2; ++endIdx) {
        const glm::vec3 center = (endIdx == 0) ? pA : pB;
        glm::vec3 prev = center + right * radius;
        for (int i = 1; i <= ringSegments; ++i) {
            const float angle = pi2 * static_cast<float>(i) / static_cast<float>(ringSegments);
            const glm::vec3 cur = center + (right * std::cos(angle) + up * std::sin(angle)) * radius;
            drawWorldLine(dl, prev, cur, vp, sw, sh, color, 1.0f);
            prev = cur;
        }
    }

    // Four connecting lines along the capsule body.
    for (int i = 0; i < 4; ++i) {
        const float angle = pi2 * static_cast<float>(i) / 4.0f;
        const glm::vec3 offset = (right * std::cos(angle) + up * std::sin(angle)) * radius;
        drawWorldLine(dl, pA + offset, pB + offset, vp, sw, sh, color, 1.0f);
    }

    // Hemisphere arcs on each end (4 meridian arcs per hemisphere).
    // pA hemisphere bulges in -axis direction, pB in +axis direction.
    for (int meridian = 0; meridian < 4; ++meridian) {
        const float theta = pi2 * static_cast<float>(meridian) / 4.0f;
        const glm::vec3 perpDir = right * std::cos(theta) + up * std::sin(theta);

        // pA hemisphere (pole at pA - axis * radius).
        {
            glm::vec3 prev = pA + perpDir * radius; // equator point
            for (int i = 1; i <= arcSegments; ++i) {
                const float phi = halfPi * static_cast<float>(i) / static_cast<float>(arcSegments);
                const glm::vec3 cur = pA + perpDir * (radius * std::cos(phi)) - axis * (radius * std::sin(phi));
                drawWorldLine(dl, prev, cur, vp, sw, sh, color, 1.0f);
                prev = cur;
            }
        }

        // pB hemisphere (pole at pB + axis * radius).
        {
            glm::vec3 prev = pB + perpDir * radius; // equator point
            for (int i = 1; i <= arcSegments; ++i) {
                const float phi = halfPi * static_cast<float>(i) / static_cast<float>(arcSegments);
                const glm::vec3 cur = pB + perpDir * (radius * std::cos(phi)) + axis * (radius * std::sin(phi));
                drawWorldLine(dl, prev, cur, vp, sw, sh, color, 1.0f);
                prev = cur;
            }
        }
    }
}

/// @brief Get a color for a body region (consistent per-region coloring).
ImU32 regionColor(BodyRegion region)
{
    switch (region) {
    case BodyRegion::Head:
        return IM_COL32(255, 50, 50, 220);  // Red
    case BodyRegion::Neck:
        return IM_COL32(255, 150, 50, 220); // Orange
    case BodyRegion::UpperTorso:
        return IM_COL32(50, 255, 50, 220);  // Green
    case BodyRegion::LowerTorso:
        return IM_COL32(50, 200, 50, 220);  // Dark green
    case BodyRegion::LeftUpperArm:
    case BodyRegion::LeftLowerArm:
        return IM_COL32(50, 150, 255, 220); // Blue
    case BodyRegion::RightUpperArm:
    case BodyRegion::RightLowerArm:
        return IM_COL32(150, 50, 255, 220); // Purple
    case BodyRegion::LeftUpperLeg:
    case BodyRegion::LeftLowerLeg:
        return IM_COL32(255, 255, 50, 220); // Yellow
    case BodyRegion::RightUpperLeg:
    case BodyRegion::RightLowerLeg:
        return IM_COL32(255, 200, 50, 220); // Gold
    default:
        return IM_COL32(255, 255, 255, 180);
    }
}

} // namespace

void DebugUI::buildHitboxUI(
    const Registry& registry, HitboxRig& hitboxRig, const glm::mat4& viewProj, float screenWidth, float screenHeight)
{
    // ── ImGui window (only when showHitboxWindow is true) ──
    if (showHitboxWindow) {
        if (ImGui::Begin("Hitbox Debug", &showHitboxWindow)) {
            ImGui::Checkbox("Draw Hitboxes", &drawHitboxOverlay);
            ImGui::Separator();

            // Count entities with hitboxes.
            int entityCount = 0;
            int capsuleCount = 0;
            registry.view<HitboxInstance>().each([&](const HitboxInstance& hb) {
                ++entityCount;
                capsuleCount += static_cast<int>(hb.capsules.size());
            });
            ImGui::Text("Entities: %d  |  Capsules: %d", entityCount, capsuleCount);
            ImGui::Separator();

            // ── Per-capsule editors ──
            if (ImGui::TreeNode("Capsule Definitions")) {
                for (size_t i = 0; i < hitboxRig.definitions.size(); ++i) {
                    auto& def = hitboxRig.definitions[i];
                    ImGui::PushID(static_cast<int>(i));

                    const ImU32 col = regionColor(def.region);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    const bool open =
                        ImGui::TreeNode("##cap", "[%zu] %s (%s)", i, def.boneName.c_str(), bodyRegionName(def.region));
                    ImGui::PopStyleColor();

                    if (open) {
                        ImGui::DragFloat3("Offset", &def.localOffset.x, 0.5f, -100.0f, 100.0f, "%.1f");
                        ImGui::DragFloat("Radius", &def.radius, 0.1f, 0.5f, 30.0f, "%.1f");
                        ImGui::DragFloat("Half Height", &def.halfHeight, 0.1f, 0.5f, 40.0f, "%.1f");
                        ImGui::DragFloat3("Axis", &def.localAxis.x, 0.05f, -1.0f, 1.0f, "%.2f");
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::Separator();

            // Damage profile display.
            if (ImGui::TreeNode("Damage Multipliers")) {
                const auto& profile = defaultDamageProfile();
                for (size_t i = 0; i < static_cast<size_t>(BodyRegion::Count); ++i) {
                    const auto region = static_cast<BodyRegion>(i);
                    ImU32 col = regionColor(region);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::Text("%-15s  %.2fx", bodyRegionName(region), static_cast<double>(profile.multipliers[i]));
                    ImGui::PopStyleColor();
                }
                ImGui::TreePop();
            }
        }
        ImGui::End();
    }

    // ── Capsule wireframe overlay (independent of window visibility) ──
    // This intentionally runs even when the window is hidden (F2 toggle)
    // so you can play with a clean viewport while still seeing hitboxes.
    if (drawHitboxOverlay) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        registry.view<HitboxInstance>().each([&](const HitboxInstance& hb) {
            for (const auto& cap : hb.capsules) {
                const ImU32 color = regionColor(cap.region);
                drawCapsuleWireframe(
                    dl, cap.pointA, cap.pointB, cap.radius, viewProj, screenWidth, screenHeight, color);
            }
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision wireframe helpers (anonymous namespace, same pattern as hitbox)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{

/// @brief Draw an AABB wireframe (12 edges).
void drawAABBWireframe(
    ImDrawList* dl, const physics::WorldAABB& box, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    const glm::vec3& mn = box.min;
    const glm::vec3& mx = box.max;

    // 8 corners.
    const glm::vec3 c[8] = {
        {mn.x, mn.y, mn.z},
        {mx.x, mn.y, mn.z},
        {mx.x, mn.y, mx.z},
        {mn.x, mn.y, mx.z},
        {mn.x, mx.y, mn.z},
        {mx.x, mx.y, mn.z},
        {mx.x, mx.y, mx.z},
        {mn.x, mx.y, mx.z},
    };

    // Bottom face.
    drawWorldLine(dl, c[0], c[1], vp, sw, sh, color);
    drawWorldLine(dl, c[1], c[2], vp, sw, sh, color);
    drawWorldLine(dl, c[2], c[3], vp, sw, sh, color);
    drawWorldLine(dl, c[3], c[0], vp, sw, sh, color);
    // Top face.
    drawWorldLine(dl, c[4], c[5], vp, sw, sh, color);
    drawWorldLine(dl, c[5], c[6], vp, sw, sh, color);
    drawWorldLine(dl, c[6], c[7], vp, sw, sh, color);
    drawWorldLine(dl, c[7], c[4], vp, sw, sh, color);
    // Vertical edges.
    drawWorldLine(dl, c[0], c[4], vp, sw, sh, color);
    drawWorldLine(dl, c[1], c[5], vp, sw, sh, color);
    drawWorldLine(dl, c[2], c[6], vp, sw, sh, color);
    drawWorldLine(dl, c[3], c[7], vp, sw, sh, color);
}

/// @brief Draw a vertical cylinder wireframe (top ring, bottom ring, 4 struts).
void drawCylinderWireframe(
    ImDrawList* dl, const physics::WorldCylinder& cyl, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    constexpr int kSegments = 16;
    constexpr float kPi2 = 6.2831853f;

    const glm::vec3 botCenter = cyl.base;
    const glm::vec3 topCenter = cyl.base + glm::vec3(0.0f, cyl.height, 0.0f);

    // Draw top and bottom rings + 4 struts.
    glm::vec3 prevBot = botCenter + glm::vec3(cyl.radius, 0.0f, 0.0f);
    glm::vec3 prevTop = topCenter + glm::vec3(cyl.radius, 0.0f, 0.0f);

    for (int i = 1; i <= kSegments; ++i) {
        const float angle = kPi2 * static_cast<float>(i) / static_cast<float>(kSegments);
        const float cs = std::cos(angle);
        const float sn = std::sin(angle);
        const glm::vec3 offset(cyl.radius * cs, 0.0f, cyl.radius * sn);

        const glm::vec3 curBot = botCenter + offset;
        const glm::vec3 curTop = topCenter + offset;

        drawWorldLine(dl, prevBot, curBot, vp, sw, sh, color);
        drawWorldLine(dl, prevTop, curTop, vp, sw, sh, color);

        // 4 vertical struts at 0°, 90°, 180°, 270°.
        if (i % (kSegments / 4) == 0)
            drawWorldLine(dl, curBot, curTop, vp, sw, sh, color);

        prevBot = curBot;
        prevTop = curTop;
    }
}

/// @brief Draw a sphere wireframe (3 orthogonal great circles).
void drawSphereWireframe(
    ImDrawList* dl, const physics::WorldSphere& sph, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    constexpr int kSegments = 16;
    constexpr float kPi2 = 6.2831853f;

    const glm::vec3& c = sph.center;
    const float r = sph.radius;

    // XY ring.
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = kPi2 * static_cast<float>(i) / static_cast<float>(kSegments);
        const float a1 = kPi2 * static_cast<float>(i + 1) / static_cast<float>(kSegments);
        drawWorldLine(dl,
                      c + glm::vec3(r * std::cos(a0), r * std::sin(a0), 0.0f),
                      c + glm::vec3(r * std::cos(a1), r * std::sin(a1), 0.0f),
                      vp,
                      sw,
                      sh,
                      color);
    }
    // XZ ring.
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = kPi2 * static_cast<float>(i) / static_cast<float>(kSegments);
        const float a1 = kPi2 * static_cast<float>(i + 1) / static_cast<float>(kSegments);
        drawWorldLine(dl,
                      c + glm::vec3(r * std::cos(a0), 0.0f, r * std::sin(a0)),
                      c + glm::vec3(r * std::cos(a1), 0.0f, r * std::sin(a1)),
                      vp,
                      sw,
                      sh,
                      color);
    }
    // YZ ring.
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = kPi2 * static_cast<float>(i) / static_cast<float>(kSegments);
        const float a1 = kPi2 * static_cast<float>(i + 1) / static_cast<float>(kSegments);
        drawWorldLine(dl,
                      c + glm::vec3(0.0f, r * std::cos(a0), r * std::sin(a0)),
                      c + glm::vec3(0.0f, r * std::cos(a1), r * std::sin(a1)),
                      vp,
                      sw,
                      sh,
                      color);
    }
}

/// @brief Draw a convex brush wireframe by intersecting plane triples to find vertices,
/// then drawing edges between vertices that share two planes.
void drawBrushWireframe(
    ImDrawList* dl, const physics::WorldBrush& brush, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    const int n = brush.planeCount;
    if (n < 3)
        return;

    // Find vertices: intersection of every triple of planes.
    // A vertex is valid if it lies inside all other planes (dot(n,v) <= d + eps).
    struct BrushVert
    {
        glm::vec3 pos;
        int p0, p1, p2; // Plane indices that form this vertex.
    };
    std::vector<BrushVert> verts;

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            for (int k = j + 1; k < n; ++k) {
                const auto& pi = brush.planes[i];
                const auto& pj = brush.planes[j];
                const auto& pk = brush.planes[k];

                // Solve 3×3 system: dot(n_i, v) = d_i, etc.
                const glm::vec3 cross_jk = glm::cross(pj.normal, pk.normal);
                const float denom = glm::dot(pi.normal, cross_jk);
                if (std::abs(denom) < 1e-6f)
                    continue; // Parallel or degenerate.

                const glm::vec3 v =
                    (pi.distance * glm::cross(pj.normal, pk.normal) + pj.distance * glm::cross(pk.normal, pi.normal) +
                     pk.distance * glm::cross(pi.normal, pj.normal)) /
                    denom;

                // Check if v is inside all planes (normals point outward, solid is dot < d).
                bool inside = true;
                for (int m = 0; m < n; ++m) {
                    if (m == i || m == j || m == k)
                        continue;
                    if (glm::dot(brush.planes[m].normal, v) > brush.planes[m].distance + 0.1f) {
                        inside = false;
                        break;
                    }
                }
                if (inside)
                    verts.push_back({v, i, j, k});
            }
        }
    }

    // Draw edges: two vertices share an edge if they share exactly 2 plane indices.
    for (size_t a = 0; a < verts.size(); ++a) {
        for (size_t b = a + 1; b < verts.size(); ++b) {
            int shared = 0;
            const int planesA[3] = {verts[a].p0, verts[a].p1, verts[a].p2};
            const int planesB[3] = {verts[b].p0, verts[b].p1, verts[b].p2};
            for (int pa : planesA)
                for (int pb : planesB)
                    if (pa == pb)
                        ++shared;
            if (shared == 2)
                drawWorldLine(dl, verts[a].pos, verts[b].pos, vp, sw, sh, color, 1.5f);
        }
    }
}

/// @brief Draw an infinite plane as a finite grid quad (±extent from origin along the plane).
void drawPlaneGrid(ImDrawList* dl, const physics::Plane& plane, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    constexpr float kExtent = 2000.0f;
    constexpr int kDivisions = 8;

    // Build tangent frame on the plane.
    glm::vec3 tangent = (std::abs(plane.normal.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    tangent = glm::normalize(tangent - plane.normal * glm::dot(tangent, plane.normal));
    const glm::vec3 bitangent = glm::cross(plane.normal, tangent);

    // Plane origin: closest point to world origin.
    const glm::vec3 origin = plane.normal * plane.distance;

    // Draw grid lines.
    const float step = 2.0f * kExtent / static_cast<float>(kDivisions);
    for (int i = 0; i <= kDivisions; ++i) {
        const float t = -kExtent + step * static_cast<float>(i);

        // Lines along tangent direction.
        const glm::vec3 a = origin + bitangent * t - tangent * kExtent;
        const glm::vec3 b = origin + bitangent * t + tangent * kExtent;
        drawWorldLine(dl, a, b, vp, sw, sh, color, (i == kDivisions / 2) ? 2.0f : 1.0f);

        // Lines along bitangent direction.
        const glm::vec3 c = origin + tangent * t - bitangent * kExtent;
        const glm::vec3 d = origin + tangent * t + bitangent * kExtent;
        drawWorldLine(dl, c, d, vp, sw, sh, color, (i == kDivisions / 2) ? 2.0f : 1.0f);
    }
}

/// @brief Draw a triangle mesh wireframe — every triangle edge.
void drawTriMeshWireframe(
    ImDrawList* dl, const physics::WorldTriMesh& mesh, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    const size_t triCount = mesh.indices.size() / 3;
    for (size_t i = 0; i < triCount; ++i) {
        const glm::vec3& v0 = mesh.vertices[mesh.indices[i * 3 + 0]];
        const glm::vec3& v1 = mesh.vertices[mesh.indices[i * 3 + 1]];
        const glm::vec3& v2 = mesh.vertices[mesh.indices[i * 3 + 2]];
        drawWorldLine(dl, v0, v1, vp, sw, sh, color);
        drawWorldLine(dl, v1, v2, vp, sw, sh, color);
        drawWorldLine(dl, v2, v0, vp, sw, sh, color);
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// buildCollisionUI
// ─────────────────────────────────────────────────────────────────────────────
void DebugUI::buildCollisionUI(const physics::WorldGeometry& world,
                               const glm::mat4& viewProj,
                               float screenWidth,
                               float screenHeight)
{
    // ── ImGui window (only when showCollisionWindow is true) ──
    if (showCollisionWindow) {
        if (ImGui::Begin("Collision Debug", &showCollisionWindow)) {
            ImGui::Checkbox("Draw Collisions", &drawCollisionOverlay);
            ImGui::Separator();

            // Summary counts.
            ImGui::Text("Planes: %zu  |  Boxes: %zu  |  Brushes: %zu",
                        world.planes.size(),
                        world.boxes.size(),
                        world.brushes.size());
            ImGui::Text("Cylinders: %zu  |  Spheres: %zu  |  TriMeshes: %zu",
                        world.cylinders.size(),
                        world.spheres.size(),
                        world.triMeshes.size());

            // Total triangle count from all trimeshes.
            size_t totalTris = 0;
            for (const auto& tm : world.triMeshes)
                totalTris += tm.indices.size() / 3;
            if (totalTris > 0)
                ImGui::Text("Total triangles: %zu", totalTris);

            ImGui::Separator();
            ImGui::Text("Per-type visibility:");

            // Color-coded checkboxes matching the wireframe colors.
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 255, 220));
            ImGui::Checkbox("Planes", &drawCollisionPlanes);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(50, 150, 255, 220));
            ImGui::Checkbox("Boxes", &drawCollisionBoxes);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(50, 255, 50, 220));
            ImGui::Checkbox("Brushes", &drawCollisionBrushes);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 50, 220));
            ImGui::Checkbox("Cylinders", &drawCollisionCylinders);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 200, 220));
            ImGui::Checkbox("Spheres", &drawCollisionSpheres);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 50, 255, 220));
            ImGui::Checkbox("TriMeshes", &drawCollisionTriMeshes);
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    // ── Wireframe overlay (independent of window visibility) ──
    if (!drawCollisionOverlay)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    if (drawCollisionPlanes) {
        const ImU32 planeColor = IM_COL32(0, 255, 255, 140);
        for (const auto& p : world.planes)
            drawPlaneGrid(dl, p, viewProj, screenWidth, screenHeight, planeColor);
    }

    if (drawCollisionBoxes) {
        const ImU32 boxColor = IM_COL32(50, 150, 255, 220);
        for (const auto& b : world.boxes)
            drawAABBWireframe(dl, b, viewProj, screenWidth, screenHeight, boxColor);
    }

    if (drawCollisionBrushes) {
        const ImU32 brushColor = IM_COL32(50, 255, 50, 220);
        for (const auto& b : world.brushes)
            drawBrushWireframe(dl, b, viewProj, screenWidth, screenHeight, brushColor);
    }

    if (drawCollisionCylinders) {
        const ImU32 cylColor = IM_COL32(255, 200, 50, 220);
        for (const auto& c : world.cylinders)
            drawCylinderWireframe(dl, c, viewProj, screenWidth, screenHeight, cylColor);
    }

    if (drawCollisionSpheres) {
        const ImU32 sphColor = IM_COL32(255, 100, 200, 220);
        for (const auto& s : world.spheres)
            drawSphereWireframe(dl, s, viewProj, screenWidth, screenHeight, sphColor);
    }

    if (drawCollisionTriMeshes) {
        const ImU32 triColor = IM_COL32(200, 50, 255, 220);
        for (const auto& tm : world.triMeshes)
            drawTriMeshWireframe(dl, tm, viewProj, screenWidth, screenHeight, triColor);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Contact debug overlay helpers (anonymous namespace, file-local)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{

/// @brief Colour palette for contact sources.  Sweep contacts are bright;
/// depen contacts are warmer so the two categories are visually distinct.
ImU32 contactSourceColor(physics::debug::ContactSource src)
{
    using Src = physics::debug::ContactSource;
    switch (src) {
    case Src::PlaneSweep:
        return IM_COL32(120, 220, 255, 230);
    case Src::BoxSweep:
        return IM_COL32(50, 150, 255, 230);
    case Src::BrushSweep:
        return IM_COL32(60, 240, 90, 230);
    case Src::CylinderSweep:
        return IM_COL32(255, 200, 80, 230);
    case Src::SphereSweep:
        return IM_COL32(255, 110, 200, 230);
    case Src::TriMeshSweep:
        return IM_COL32(210, 80, 255, 230);
    case Src::PlaneDepen:
        return IM_COL32(255, 90, 90, 230);
    case Src::BoxDepen:
        return IM_COL32(255, 130, 60, 230);
    case Src::BrushDepen:
        return IM_COL32(255, 220, 50, 230);
    case Src::CylinderDepen:
        return IM_COL32(255, 90, 40, 230);
    case Src::SphereDepen:
        return IM_COL32(255, 50, 130, 230);
    case Src::TriMeshDepen:
        return IM_COL32(255, 50, 50, 230);
    case Src::Count:
        break;
    }
    return IM_COL32(255, 255, 255, 220);
}

/// @brief Visibility filter from the per-source toggles on `DebugUI`.
bool contactSourceVisible(const DebugUI& ui, physics::debug::ContactSource src)
{
    using Src = physics::debug::ContactSource;
    switch (src) {
    case Src::PlaneSweep:
        return ui.drawContactPlaneSweep;
    case Src::BoxSweep:
        return ui.drawContactBoxSweep;
    case Src::BrushSweep:
        return ui.drawContactBrushSweep;
    case Src::CylinderSweep:
        return ui.drawContactCylinderSweep;
    case Src::SphereSweep:
        return ui.drawContactSphereSweep;
    case Src::TriMeshSweep:
        return ui.drawContactTriMeshSweep;
    case Src::PlaneDepen:
        return ui.drawContactPlaneDepen;
    case Src::BoxDepen:
        return ui.drawContactBoxDepen;
    case Src::BrushDepen:
        return ui.drawContactBrushDepen;
    case Src::CylinderDepen:
        return ui.drawContactCylinderDepen;
    case Src::SphereDepen:
        return ui.drawContactSphereDepen;
    case Src::TriMeshDepen:
        return ui.drawContactTriMeshDepen;
    case Src::Count:
        break;
    }
    return true;
}

/// @brief Draw an arrow (3D line + 2D arrowhead) from `p` along `dir` with `length` world units.
void drawArrow(ImDrawList* dl,
               glm::vec3 p,
               glm::vec3 dir,
               float length,
               const glm::mat4& vp,
               float sw,
               float sh,
               ImU32 color,
               float thickness = 1.5f)
{
    const glm::vec3 tip = p + dir * length;
    drawWorldLine(dl, p, tip, vp, sw, sh, color, thickness);

    // Small 2D arrowhead at the tip.
    ImVec2 sTail;
    ImVec2 sTip;
    if (!worldToScreen(p, vp, sw, sh, sTail))
        return;
    if (!worldToScreen(tip, vp, sw, sh, sTip))
        return;

    const float dx = sTip.x - sTail.x;
    const float dy = sTip.y - sTail.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f)
        return;

    const float headLen = std::min(10.0f, len * 0.35f);
    const float invLen = 1.0f / len;
    const float ux = dx * invLen;
    const float uy = dy * invLen;
    // Two perpendicular offsets for the head wings.
    const float px = -uy;
    const float py = ux;
    const ImVec2 wingA{sTip.x - ux * headLen + px * headLen * 0.5f, sTip.y - uy * headLen + py * headLen * 0.5f};
    const ImVec2 wingB{sTip.x - ux * headLen - px * headLen * 0.5f, sTip.y - uy * headLen - py * headLen * 0.5f};
    dl->AddLine(sTip, wingA, color, thickness);
    dl->AddLine(sTip, wingB, color, thickness);
}

/// @brief Draw a small filled disc at a world position (or skip if behind camera).
void drawPointMarker(ImDrawList* dl, glm::vec3 p, const glm::mat4& vp, float sw, float sh, ImU32 color, float radiusPx)
{
    ImVec2 s;
    if (!worldToScreen(p, vp, sw, sh, s))
        return;
    dl->AddCircleFilled(s, radiusPx, color, 8);
    // Tiny dark ring so the marker reads against bright backgrounds.
    dl->AddCircle(s, radiusPx + 0.5f, IM_COL32(0, 0, 0, 200), 8, 1.0f);
}

const char* contactSourceLabel(physics::debug::ContactSource src)
{
    using Src = physics::debug::ContactSource;
    switch (src) {
    case Src::PlaneSweep:
        return "Plane sweep";
    case Src::BoxSweep:
        return "Box sweep";
    case Src::BrushSweep:
        return "Brush sweep";
    case Src::CylinderSweep:
        return "Cylinder sweep";
    case Src::SphereSweep:
        return "Sphere sweep";
    case Src::TriMeshSweep:
        return "TriMesh sweep";
    case Src::PlaneDepen:
        return "Plane depen";
    case Src::BoxDepen:
        return "Box depen";
    case Src::BrushDepen:
        return "Brush depen";
    case Src::CylinderDepen:
        return "Cylinder depen";
    case Src::SphereDepen:
        return "Sphere depen";
    case Src::TriMeshDepen:
        return "TriMesh depen";
    case Src::Count:
        break;
    }
    return "?";
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// buildContactDebugUI
// ─────────────────────────────────────────────────────────────────────────────
void DebugUI::buildContactDebugUI(const glm::mat4& viewProj, float screenWidth, float screenHeight)
{
    // ── ImGui window (only when showContactDebugWindow is true) ──
    if (showContactDebugWindow) {
        if (ImGui::Begin("Contact Debug", &showContactDebugWindow)) {
            bool enabled = physics::debug::isEnabled();
            if (ImGui::Checkbox("Capture contacts (records every hit/MTV)", &enabled))
                physics::debug::setEnabled(enabled);

            ImGui::Checkbox("Draw overlay", &drawContactOverlay);
            ImGui::Separator();

            ImGui::SliderFloat("Normal length (u)", &contactNormalLength, 1.0f, 200.0f, "%.1f");
            ImGui::SliderFloat("Point radius (px)", &contactPointRadius, 1.0f, 12.0f, "%.1f");

            ImGui::Separator();
            ImGui::Checkbox("Draw welded edges (green=active, red=welded)", &drawMeshEdgeOverlay);
            ImGui::Checkbox("Draw welded vertices", &drawMeshVertexOverlay);

            ImGui::Separator();
            ImGui::TextWrapped(
                "Phase telemetry: per-tick player physics state → phase-diag-*.csv in working dir. "
                "Look for the SuspectedPhase column = 1 to find phase-through moments. "
                "Open the CSV in any spreadsheet; sort by SuspectedPhase, DeepPenetration, or BumpExhausted.");
            bool phaseDiag = physics::diag::isEnabled();
            if (ImGui::Checkbox("Record phase telemetry (writes CSV)", &phaseDiag))
                physics::diag::setEnabled(phaseDiag);

            const auto k_contacts = physics::debug::contacts();

            // Tally per source for a fast diagnostic at a glance.
            int counts[static_cast<size_t>(physics::debug::ContactSource::Count)] = {};
            for (const auto& c : k_contacts) {
                const auto idx = static_cast<size_t>(c.source);
                if (idx < std::size(counts))
                    ++counts[idx];
            }

            ImGui::Separator();
            ImGui::Text("Total contacts this frame: %zu", k_contacts.size());

            ImGui::SeparatorText("Per-source");
            const auto sources = {
                physics::debug::ContactSource::PlaneSweep,
                physics::debug::ContactSource::BoxSweep,
                physics::debug::ContactSource::BrushSweep,
                physics::debug::ContactSource::CylinderSweep,
                physics::debug::ContactSource::SphereSweep,
                physics::debug::ContactSource::TriMeshSweep,
                physics::debug::ContactSource::PlaneDepen,
                physics::debug::ContactSource::BoxDepen,
                physics::debug::ContactSource::BrushDepen,
                physics::debug::ContactSource::CylinderDepen,
                physics::debug::ContactSource::SphereDepen,
                physics::debug::ContactSource::TriMeshDepen,
            };
            bool* toggles[] = {
                &drawContactPlaneSweep,
                &drawContactBoxSweep,
                &drawContactBrushSweep,
                &drawContactCylinderSweep,
                &drawContactSphereSweep,
                &drawContactTriMeshSweep,
                &drawContactPlaneDepen,
                &drawContactBoxDepen,
                &drawContactBrushDepen,
                &drawContactCylinderDepen,
                &drawContactSphereDepen,
                &drawContactTriMeshDepen,
            };
            int sIdx = 0;
            for (physics::debug::ContactSource s : sources) {
                ImGui::PushStyleColor(ImGuiCol_Text, contactSourceColor(s));
                ImGui::Checkbox(contactSourceLabel(s), toggles[sIdx]);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::Text("(%d)", counts[static_cast<size_t>(s)]);
                ++sIdx;
            }
        }
        ImGui::End();
    }

    // ── Overlay (independent of window visibility) ──
    if (!drawContactOverlay)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const auto k_contacts = physics::debug::contacts();
    for (const auto& c : k_contacts) {
        if (!contactSourceVisible(*this, c.source))
            continue;

        const ImU32 color = contactSourceColor(c.source);

        // Depen arrows are stretched by their depth so deep penetrations are visually loud.
        const float lengthMul = (c.depth > 0.0f) ? std::clamp(c.depth / 8.0f, 0.5f, 4.0f) : 1.0f;
        const float length = contactNormalLength * lengthMul;

        drawPointMarker(dl, c.point, viewProj, screenWidth, screenHeight, color, contactPointRadius);
        drawArrow(dl, c.point, c.normal, length, viewProj, screenWidth, screenHeight, color);
    }

    // Phase 2 welded-edge / vertex overlay.  Walks every active-world trimesh,
    // draws each triangle edge green-or-red by its `edgeActive` bit, and each
    // vertex as a small dot coloured the same way.  Validates the cooked
    // welding data by eye against the underlying mesh wireframe.
    if (drawMeshEdgeOverlay || drawMeshVertexOverlay) {
        const physics::WorldGeometry& world = physics::activeWorld();
        const ImU32 active = IM_COL32(60, 240, 90, 230);
        const ImU32 welded = IM_COL32(255, 80, 80, 220);

        for (const physics::WorldTriMesh& tm : world.triMeshes) {
            const size_t triCount = tm.indices.size() / 3u;
            // Skip meshes whose welding pass has not run yet.
            if (tm.edgeActive.size() != triCount || tm.vertActive.size() != triCount)
                continue;

            for (size_t t = 0; t < triCount; ++t) {
                const glm::vec3& v0 = tm.vertices[tm.indices[t * 3u + 0u]];
                const glm::vec3& v1 = tm.vertices[tm.indices[t * 3u + 1u]];
                const glm::vec3& v2 = tm.vertices[tm.indices[t * 3u + 2u]];
                const uint8_t e = tm.edgeActive[t];
                const uint8_t vMask = tm.vertActive[t];

                if (drawMeshEdgeOverlay) {
                    drawWorldLine(dl, v0, v1, viewProj, screenWidth, screenHeight, (e & 1u) ? active : welded, 1.5f);
                    drawWorldLine(dl, v1, v2, viewProj, screenWidth, screenHeight, (e & 2u) ? active : welded, 1.5f);
                    drawWorldLine(dl, v2, v0, viewProj, screenWidth, screenHeight, (e & 4u) ? active : welded, 1.5f);
                }
                if (drawMeshVertexOverlay) {
                    drawPointMarker(dl, v0, viewProj, screenWidth, screenHeight, (vMask & 1u) ? active : welded, 3.0f);
                    drawPointMarker(dl, v1, viewProj, screenWidth, screenHeight, (vMask & 2u) ? active : welded, 3.0f);
                    drawPointMarker(dl, v2, viewProj, screenWidth, screenHeight, (vMask & 4u) ? active : welded, 3.0f);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildWeaponSpawnerUI
// ─────────────────────────────────────────────────────────────────────────────
void DebugUI::buildWeaponSpawnerUI(const Registry& registry,
                                   const glm::mat4& viewProj,
                                   float screenWidth,
                                   float screenHeight)
{
    // ── ImGui window (only when showWeaponSpawnerWindow is true) ──
    if (showWeaponSpawnerWindow) {
        if (ImGui::Begin("Weapon Spawner Debug", &showWeaponSpawnerWindow)) {
            ImGui::Checkbox("Draw Spawner Boxes", &drawWeaponSpawnerOverlay);
            ImGui::Separator();

            // Count spawners.
            int totalSpawners = 0;
            int activeSpawners = 0;
            auto spawnerView = registry.view<WeaponSpawner, Position, CollisionShape>();
            for (auto e : spawnerView) {
                ++totalSpawners;
                if (spawnerView.get<WeaponSpawner>(e).hasWeapon)
                    ++activeSpawners;
            }
            ImGui::Text("Spawners: %d  |  Active: %d  |  On Cooldown: %d",
                        totalSpawners,
                        activeSpawners,
                        totalSpawners - activeSpawners);
            ImGui::Separator();

            // Per-spawner details.
            int idx = 0;
            for (auto e : spawnerView) {
                const auto& spawner = spawnerView.get<WeaponSpawner>(e);
                const auto& pos = spawnerView.get<Position>(e);
                const auto& shape = spawnerView.get<CollisionShape>(e);

                const char* typeName = weaponTypeName(spawner.type);

                char label[64];
                std::snprintf(label, sizeof(label), "Spawner #%d (%s)", idx, typeName);
                if (ImGui::TreeNode(label)) {
                    ImGui::Text("Position: (%.0f, %.0f, %.0f)",
                                static_cast<double>(pos.value.x),
                                static_cast<double>(pos.value.y),
                                static_cast<double>(pos.value.z));
                    ImGui::Text("Box half-extents: (%.0f, %.0f, %.0f)",
                                static_cast<double>(shape.halfExtents.x),
                                static_cast<double>(shape.halfExtents.y),
                                static_cast<double>(shape.halfExtents.z));
                    ImGui::Text("Has weapon: %s", spawner.hasWeapon ? "YES" : "no");
                    if (!spawner.hasWeapon)
                        ImGui::Text("Cooldown: %.1fs", static_cast<double>(spawner.spawnCooldown));
                    ImGui::TreePop();
                }
                ++idx;
            }
        }
        ImGui::End();
    }

    // ── Wireframe overlay (independent of window visibility) ──
    if (!drawWeaponSpawnerOverlay)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    auto spawnerView = registry.view<WeaponSpawner, Position, CollisionShape>();
    for (auto e : spawnerView) {
        const auto& spawner = spawnerView.get<WeaponSpawner>(e);
        const auto& pos = spawnerView.get<Position>(e);
        const auto& shape = spawnerView.get<CollisionShape>(e);

        // Green = weapon available, red/orange = on cooldown.
        const ImU32 boxColor = spawner.hasWeapon ? IM_COL32(50, 255, 50, 200) : IM_COL32(255, 100, 50, 160);

        // Draw AABB wireframe around the spawner's collision volume.
        const glm::vec3 mn = pos.value - shape.halfExtents;
        const glm::vec3 mx = pos.value + shape.halfExtents;
        const glm::vec3 corners[8] = {
            {mn.x, mn.y, mn.z},
            {mx.x, mn.y, mn.z},
            {mx.x, mn.y, mx.z},
            {mn.x, mn.y, mx.z},
            {mn.x, mx.y, mn.z},
            {mx.x, mx.y, mn.z},
            {mx.x, mx.y, mx.z},
            {mn.x, mx.y, mx.z},
        };
        // 12 edges of the AABB.
        static constexpr int edges[12][2] = {
            {0, 1},
            {1, 2},
            {2, 3},
            {3, 0}, // bottom
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4}, // top
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7}, // verticals
        };
        for (const auto& edge : edges)
            drawWorldLine(dl, corners[edge[0]], corners[edge[1]], viewProj, screenWidth, screenHeight, boxColor, 2.0f);

        // Draw a cross marker at the spawn position (center point).
        ImVec2 sp;
        if (worldToScreen(pos.value, viewProj, screenWidth, screenHeight, sp)) {
            constexpr float k_crossLen = 10.0f;
            const ImU32 markerColor = IM_COL32(255, 255, 0, 220);
            dl->AddLine({sp.x - k_crossLen, sp.y}, {sp.x + k_crossLen, sp.y}, markerColor, 2.0f);
            dl->AddLine({sp.x, sp.y - k_crossLen}, {sp.x, sp.y + k_crossLen}, markerColor, 2.0f);
            dl->AddCircle(sp, k_crossLen, markerColor, 12, 1.5f);

            // Label with weapon type name.
            const char* typeName = weaponTypeName(spawner.type);
            dl->AddText({sp.x + k_crossLen + 4.0f, sp.y - 6.0f}, markerColor, typeName);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildSpawnPointUI
// ─────────────────────────────────────────────────────────────────────────────
void DebugUI::buildSpawnPointUI(const Registry& registry,
                                const glm::mat4& viewProj,
                                float screenWidth,
                                float screenHeight)
{
    // ── ImGui window (only when showSpawnPointWindow is true) ──
    if (showSpawnPointWindow) {
        if (ImGui::Begin("Spawn Point Debug", &showSpawnPointWindow)) {
            ImGui::Checkbox("Draw Spawn Markers", &drawSpawnPointOverlay);
            ImGui::Separator();

            // Count spawn points.
            int totalSpawns = 0;
            int availableSpawns = 0;
            auto spawnView = registry.view<RespawnPoint, Position>();
            for (auto e : spawnView) {
                ++totalSpawns;
                if (spawnView.get<RespawnPoint>(e).available)
                    ++availableSpawns;
            }
            ImGui::Text("Spawn Points: %d  |  Available: %d  |  On Cooldown: %d",
                        totalSpawns,
                        availableSpawns,
                        totalSpawns - availableSpawns);
            ImGui::Separator();

            // Per-spawn-point details.
            int idx = 0;
            for (auto e : spawnView) {
                const auto& sp = spawnView.get<RespawnPoint>(e);
                const auto& pos = spawnView.get<Position>(e);

                char label[64];
                std::snprintf(label, sizeof(label), "Spawn #%d", idx);
                if (ImGui::TreeNode(label)) {
                    ImGui::Text("Position: (%.0f, %.0f, %.0f)",
                                static_cast<double>(pos.value.x),
                                static_cast<double>(pos.value.y),
                                static_cast<double>(pos.value.z));
                    if (sp.available) {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Available");
                    } else {
                        ImGui::TextColored(
                            ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "Cooldown: %.1fs", static_cast<double>(sp.cooldown));
                    }
                    ImGui::TreePop();
                }
                ++idx;
            }
        }
        ImGui::End();
    }

    // ── Marker overlay (independent of window visibility) ──
    if (!drawSpawnPointOverlay)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    auto spawnView = registry.view<RespawnPoint, Position>();
    int idx = 0;
    for (auto e : spawnView) {
        const auto& sp = spawnView.get<RespawnPoint>(e);
        const auto& pos = spawnView.get<Position>(e);

        // Green = available, orange = on cooldown.
        const ImU32 markerColor = sp.available ? IM_COL32(50, 255, 50, 220) : IM_COL32(255, 140, 30, 200);

        // Draw a diamond marker at the spawn position.
        ImVec2 center;
        if (worldToScreen(pos.value, viewProj, screenWidth, screenHeight, center)) {
            constexpr float k_size = 12.0f;
            // Diamond shape (rotated square)
            dl->AddQuadFilled({center.x, center.y - k_size},
                              {center.x + k_size, center.y},
                              {center.x, center.y + k_size},
                              {center.x - k_size, center.y},
                              markerColor);
            // Outline
            dl->AddQuad({center.x, center.y - k_size},
                        {center.x + k_size, center.y},
                        {center.x, center.y + k_size},
                        {center.x - k_size, center.y},
                        IM_COL32(255, 255, 255, 200),
                        2.0f);

            // Label with spawn index and status.
            char text[48];
            if (sp.available) {
                std::snprintf(text, sizeof(text), "Spawn #%d", idx);
            } else {
                std::snprintf(text, sizeof(text), "Spawn #%d (%.1fs)", idx, static_cast<double>(sp.cooldown));
            }
            dl->AddText({center.x + k_size + 4.0f, center.y - 6.0f}, IM_COL32(255, 255, 255, 220), text);
        }

        // Draw a vertical "pole" line from the ground (y=0) up to the spawn height
        // so the spawn is easy to spot in 3D space.
        const glm::vec3 groundPoint = {pos.value.x, 0.0f, pos.value.z};
        drawWorldLine(dl, groundPoint, pos.value, viewProj, screenWidth, screenHeight, markerColor, 1.5f);

        ++idx;
    }
}

// ── PR-20: Shot debug visualizer (CSGO sv_showimpacts) ──────────────────

namespace
{

/// @brief Find the ring slot whose pair matches `tick`.  Returns -1 if
/// no entry matches.  Linear scan; ring is small (≤ 30).
int findShotPairByTick(const std::array<DebugUI::ShotDebugPair, DebugUI::k_shotRingMax>& ring,
                       int liveCount,
                       std::uint32_t tick)
{
    const int n = std::min(liveCount, DebugUI::k_shotRingMax);
    for (int i = 0; i < n; ++i) {
        const auto& pair = ring[static_cast<size_t>(i)];
        if (pair.shotInputTick == tick && (pair.hasClient || pair.hasServer))
            return i;
    }
    return -1;
}

/// @brief Solid-color ImU32 helper that lets the user override alpha.
ImU32 col(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    return IM_COL32(r, g, b, a);
}

/// @brief PR-20.5: line-draw with near-plane clipping in homogeneous
/// clip space, so partially-visible lines still render their on-screen
/// portion.  The plain `drawWorldLine` drops the entire line if either
/// endpoint has clip.w ≤ ε (behind the camera or near plane), which
/// makes the shot-debug ray vanish whenever the player walks toward
/// its hit point.  Long rays straddle the near plane often enough
/// that this is a daily issue.
void drawWorldLineClipped(ImDrawList* dl,
                          glm::vec3 a,
                          glm::vec3 b,
                          const glm::mat4& vp,
                          float sw,
                          float sh,
                          ImU32 color,
                          float thickness = 1.0f)
{
    constexpr float eps = 0.0001f;
    glm::vec4 ca = vp * glm::vec4(a, 1.0f);
    glm::vec4 cb = vp * glm::vec4(b, 1.0f);
    const bool aFront = ca.w > eps;
    const bool bFront = cb.w > eps;
    if (!aFront && !bFront)
        return;
    if (!aFront) {
        const float t = (eps - ca.w) / (cb.w - ca.w);
        ca = ca + (cb - ca) * t;
    }
    if (!bFront) {
        const float t = (eps - cb.w) / (ca.w - cb.w);
        cb = cb + (ca - cb) * t;
    }
    const float invWa = 1.0f / ca.w;
    const float invWb = 1.0f / cb.w;
    const ImVec2 sa{(0.5f + 0.5f * ca.x * invWa) * sw, (0.5f - 0.5f * ca.y * invWa) * sh};
    const ImVec2 sb{(0.5f + 0.5f * cb.x * invWb) * sw, (0.5f - 0.5f * cb.y * invWb) * sh};
    dl->AddLine(sa, sb, color, thickness);
}

/// @brief Capsule wireframe variant that uses the near-plane-clipping
/// line helper.  Otherwise identical to the existing
/// `drawCapsuleWireframe`.  Necessary for the shot-debug overlay
/// because rewound capsules are often near-camera (the player just
/// shot at the enemy on their screen) and the unclipped wireframe
/// drops segments that cross the near plane, producing visibly
/// fragmented capsule wires.
void drawCapsuleWireframeClipped(
    ImDrawList* dl, glm::vec3 pA, glm::vec3 pB, float radius, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    glm::vec3 axis = pB - pA;
    const float axisLen = glm::length(axis);
    if (axisLen < 0.001f)
        return;
    axis /= axisLen;
    glm::vec3 up = (std::abs(axis.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(axis, up));
    up = glm::normalize(glm::cross(right, axis));
    constexpr int ringSegments = 12;
    constexpr int arcSegments = 8;
    constexpr float pi2 = 6.2831853f;
    constexpr float halfPi = 1.5707963f;
    for (int endIdx = 0; endIdx < 2; ++endIdx) {
        const glm::vec3 center = (endIdx == 0) ? pA : pB;
        glm::vec3 prev = center + right * radius;
        for (int i = 1; i <= ringSegments; ++i) {
            const float angle = pi2 * static_cast<float>(i) / static_cast<float>(ringSegments);
            const glm::vec3 cur = center + (right * std::cos(angle) + up * std::sin(angle)) * radius;
            drawWorldLineClipped(dl, prev, cur, vp, sw, sh, color, 1.0f);
            prev = cur;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const float angle = pi2 * static_cast<float>(i) / 4.0f;
        const glm::vec3 offset = (right * std::cos(angle) + up * std::sin(angle)) * radius;
        drawWorldLineClipped(dl, pA + offset, pB + offset, vp, sw, sh, color, 1.0f);
    }
    for (int meridian = 0; meridian < 4; ++meridian) {
        const float theta = pi2 * static_cast<float>(meridian) / 4.0f;
        const glm::vec3 perpDir = right * std::cos(theta) + up * std::sin(theta);
        {
            glm::vec3 prev = pA + perpDir * radius;
            for (int i = 1; i <= arcSegments; ++i) {
                const float phi = halfPi * static_cast<float>(i) / static_cast<float>(arcSegments);
                const glm::vec3 cur = pA + perpDir * (radius * std::cos(phi)) - axis * (radius * std::sin(phi));
                drawWorldLineClipped(dl, prev, cur, vp, sw, sh, color, 1.0f);
                prev = cur;
            }
        }
        {
            glm::vec3 prev = pB + perpDir * radius;
            for (int i = 1; i <= arcSegments; ++i) {
                const float phi = halfPi * static_cast<float>(i) / static_cast<float>(arcSegments);
                const glm::vec3 cur = pB + perpDir * (radius * std::cos(phi)) + axis * (radius * std::sin(phi));
                drawWorldLineClipped(dl, prev, cur, vp, sw, sh, color, 1.0f);
                prev = cur;
            }
        }
    }
}

/// @brief PR-20.4: chunky high-visibility hit marker — filled inner
/// disc + outer ring + hairline cross.  Easy to spot against busy
/// scene backgrounds without being so big it dominates the view.
void drawHitMarker(ImDrawList* dl, glm::vec3 world, const glm::mat4& vp, float sw, float sh, ImU32 color)
{
    ImVec2 sp;
    if (!worldToScreen(world, vp, sw, sh, sp))
        return;
    constexpr float k_outerRadius = 14.0f;
    constexpr float k_innerRadius = 6.0f;
    constexpr float k_crossLen = 18.0f;
    dl->AddCircleFilled(sp, k_innerRadius, color);
    dl->AddCircle(sp, k_outerRadius, color, 16, 2.0f);
    dl->AddLine({sp.x - k_crossLen, sp.y}, {sp.x + k_crossLen, sp.y}, color, 1.0f);
    dl->AddLine({sp.x, sp.y - k_crossLen}, {sp.x, sp.y + k_crossLen}, color, 1.0f);
}

} // namespace

void DebugUI::pushClientShot(const net::shotdebug::ShotDebugCapture& cap)
{
    int idx = findShotPairByTick(shotRing, shotRingCount, cap.shotInputTick);
    if (idx < 0) {
        // New entry — claim the head slot, advance.
        idx = shotRingHead;
        shotRing[static_cast<size_t>(idx)] = {};
        shotRing[static_cast<size_t>(idx)].shotInputTick = cap.shotInputTick;
        shotRingHead = (shotRingHead + 1) % k_shotRingMax;
        if (shotRingCount < k_shotRingMax)
            ++shotRingCount;
    }
    shotRing[static_cast<size_t>(idx)].clientView = cap;
    shotRing[static_cast<size_t>(idx)].hasClient = true;
}

void DebugUI::pushServerShot(const net::shotdebug::ShotDebugCapture& cap)
{
    int idx = findShotPairByTick(shotRing, shotRingCount, cap.shotInputTick);
    if (idx < 0) {
        idx = shotRingHead;
        shotRing[static_cast<size_t>(idx)] = {};
        shotRing[static_cast<size_t>(idx)].shotInputTick = cap.shotInputTick;
        shotRingHead = (shotRingHead + 1) % k_shotRingMax;
        if (shotRingCount < k_shotRingMax)
            ++shotRingCount;
    }
    shotRing[static_cast<size_t>(idx)].serverView = cap;
    shotRing[static_cast<size_t>(idx)].hasServer = true;
}

void DebugUI::buildShotDebugUI(const glm::mat4& viewProj, float screenWidth, float screenHeight)
{
    if (showShotDebugWindow) {
        if (ImGui::Begin("Shot Debug (sv_showimpacts)", &showShotDebugWindow)) {
            ImGui::Checkbox("Draw 3D overlay", &drawShotDebugOverlay);
            ImGui::SliderInt("Visible last N", &shotDebugVisibleCount, 1, k_shotRingMax);
            ImGui::SliderInt("Highlight (0=all)", &shotDebugSelectIdx, 0, shotDebugVisibleCount);
            // PR-20.3: dropdown to filter the 3D overlay to only the
            // client side or server side.  Useful when the two
            // overlap closely and you want to inspect each in
            // isolation, or to confirm "where exactly did the server
            // resolve the hit" without the blue capsules in the way.
            const char* kViewModes[] = {"Both (blue+red)", "Client only (blue)", "Server only (red)"};
            ImGui::Combo("Show", &shotDebugViewMode, kViewModes, IM_ARRAYSIZE(kViewModes));
            ImGui::Separator();
            ImGui::TextDisabled("Blue = client view at fire time");
            ImGui::TextDisabled("Red  = server's rewound state");
            ImGui::TextDisabled("Bright = highlighted, dim = others in window");
            ImGui::Separator();

            // Per-shot summary table.  Walk newest-first by stepping
            // backwards from head.
            if (ImGui::BeginTable(
                    "##shots", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("# (newest=1)");
                ImGui::TableSetupColumn("tick");
                ImGui::TableSetupColumn("client");
                ImGui::TableSetupColumn("server");
                ImGui::TableSetupColumn("hit");
                ImGui::TableHeadersRow();
                const int show = std::min(shotRingCount, shotDebugVisibleCount);
                for (int i = 0; i < show; ++i) {
                    const int idx = (shotRingHead + k_shotRingMax - 1 - i) % k_shotRingMax;
                    const auto& p = shotRing[static_cast<size_t>(idx)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i + 1);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.shotInputTick);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", p.hasClient ? "Y" : "—");
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", p.hasServer ? "Y" : "—");
                    ImGui::TableNextColumn();
                    if (p.hasServer) {
                        if (p.serverView.hitTargetClientId == net::shotdebug::k_missClientId)
                            ImGui::TextDisabled("miss");
                        else
                            ImGui::Text("hit cid=%u r=%d",
                                        static_cast<unsigned>(p.serverView.hitTargetClientId),
                                        static_cast<int>(p.serverView.hitRegion));
                    } else {
                        ImGui::TextDisabled("…");
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    // 3D overlay — same VP/screen the hitbox debug overlay uses.  We
    // draw on the foreground draw list so capsules render OVER the
    // game (debug overlay is meant to be obvious, not subtle).
    if (!drawShotDebugOverlay)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int show = std::min(shotRingCount, shotDebugVisibleCount);
    const bool showClient = (shotDebugViewMode == 0 || shotDebugViewMode == 1);
    const bool showServer = (shotDebugViewMode == 0 || shotDebugViewMode == 2);
    for (int i = 0; i < show; ++i) {
        const int idx = (shotRingHead + k_shotRingMax - 1 - i) % k_shotRingMax;
        const auto& p = shotRing[static_cast<size_t>(idx)];

        // Dim non-highlighted shots so the user can pick out the
        // selected one without losing context of recent ones.
        const bool isHighlighted = (shotDebugSelectIdx == 0) || (shotDebugSelectIdx == (i + 1));
        const std::uint8_t alpha = isHighlighted ? 255 : 80;

        // Blue = client view at fire-time.  PR-20.5: use the
        // near-plane-clipping helpers so partially visible rays /
        // capsules still render their on-screen portion.
        if (showClient && p.hasClient) {
            const auto& c = p.clientView;
            // PR-20.6: use `c.hitPoint` directly.  Pre-PR-20.6 we
            // fell back to `origin + dir * range` whenever
            // `hitTargetClientId == k_missClientId`, which silently
            // discarded WALL hits (those leave hitTargetClientId at
            // the miss sentinel because no PLAYER was hit, even
            // though `resolveHitscanHitbox` correctly resolved the
            // wall geometry into `hit.point`).  The capture path
            // now writes `cap.hitPoint = localHit.point`
            // unconditionally, so here we simply use it.
            const glm::vec3 endPt = c.hitPoint;
            drawWorldLineClipped(
                dl, c.origin, endPt, viewProj, screenWidth, screenHeight, col(80, 160, 255, alpha), 2.0f);
            for (const auto& tgt : c.targets) {
                for (const auto& cap : tgt.capsules) {
                    drawCapsuleWireframeClipped(dl,
                                                cap.pointA,
                                                cap.pointB,
                                                cap.radius,
                                                viewProj,
                                                screenWidth,
                                                screenHeight,
                                                col(80, 160, 255, alpha));
                }
            }
            drawHitMarker(dl, endPt, viewProj, screenWidth, screenHeight, col(80, 160, 255, alpha));
        }
        // Red = server's rewound view of the same shot.
        if (showServer && p.hasServer) {
            const auto& s = p.serverView;
            // PR-20.6: same fix on the server side — server-captured
            // `s.hitPoint` is already the correct world-or-capsule
            // hit point or full-range endpoint, regardless of
            // whether a PLAYER was hit.
            const glm::vec3 endPt = s.hitPoint;
            drawWorldLineClipped(
                dl, s.origin, endPt, viewProj, screenWidth, screenHeight, col(255, 80, 80, alpha), 2.0f);
            for (const auto& tgt : s.targets) {
                for (const auto& cap : tgt.capsules) {
                    drawCapsuleWireframeClipped(dl,
                                                cap.pointA,
                                                cap.pointB,
                                                cap.radius,
                                                viewProj,
                                                screenWidth,
                                                screenHeight,
                                                col(255, 80, 80, alpha));
                }
            }
            drawHitMarker(dl, endPt, viewProj, screenWidth, screenHeight, col(255, 80, 80, alpha));
        }
    }
}

void DebugUI::render()
{
    ImGui::Render();
}
