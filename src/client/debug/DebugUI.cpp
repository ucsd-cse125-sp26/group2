/// @file DebugUI.cpp
/// @brief Implementation of the DebugUI overlay and all ImGui debug windows.

#include "debug/DebugUI.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/Client.hpp"    // for NetworkStats
#include "particles/ParticleSystem.hpp"
#include "renderer/Renderer.hpp" // for RenderToggles

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <cfloat>
#include <cmath>
#include <filesystem>
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

} // namespace

// DebugUI methods

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

void DebugUI::toggleAllPanels(std::initializer_list<bool*> externalPanels)
{
    // All window-visibility flags owned by DebugUI in one place — keep this
    // list in sync with the private members in DebugUI.hpp when adding new
    // top-level panels. Panels owned by other systems (e.g. Game's Animation
    // Tester) are passed in via externalPanels.
    bool* const ownedPanels[] = {
        &showInspector,
        &showMovementChart,
        &showBhopAnalyzer,
        &showParticleWindow_,
        &showRenderToggles,
        &showLightingControls,
        &showSkybox,
        &showNetworkStats,
        &showScoreboard_,
    };

    // If anything is currently visible (owned or external), hide everything;
    // otherwise show everything.
    bool anyVisible = false;
    for (bool* p : ownedPanels) {
        if (*p) {
            anyVisible = true;
            break;
        }
    }
    if (!anyVisible) {
        for (bool* p : externalPanels) {
            if (p && *p) {
                anyVisible = true;
                break;
            }
        }
    }
    const bool newState = !anyVisible;
    for (bool* p : ownedPanels)
        *p = newState;
    for (bool* p : externalPanels) {
        if (p)
            *p = newState;
    }
}

void DebugUI::buildUI(const Registry& registry,
                      const int tickCount,
                      float& mouseSensitivity,
                      bool& renderSeparateFromPhysics,
                      bool& inputSyncedWithPhysics,
                      bool& limitFPSToMonitor,
                      int& ssrMode,
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
                                   renderSeparateFromPhysics,
                                   inputSyncedWithPhysics,
                                   limitFPSToMonitor,
                                   ssrMode,
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

    buildWeaponUI(registry);
}

// Contents of the ECS Inspector window, factored out so the Begin/End wrapping
// lives in buildUI() and can be skipped cleanly when the window is hidden.
void DebugUI::buildInspectorContents(const Registry& registry,
                                     const int tickCount,
                                     float& mouseSensitivity,
                                     bool& renderSeparateFromPhysics,
                                     bool& inputSyncedWithPhysics,
                                     bool& limitFPSToMonitor,
                                     int& ssrMode,
                                     const float physicsHz,
                                     const float fpsCurrent,
                                     const float fpsMin,
                                     const float fpsMax,
                                     const float fps1pLow,
                                     const float fps5pLow)
{
    // Key bindings reminder
    ImGui::TextDisabled("ESC: toggle mouse  |  Q: quit  |  F2: toggle all panels");
    ImGui::Separator();

    // Settings
    ImGui::SeparatorText("Settings");

    // Logarithmic slider so both ends of the range are equally reachable.
    ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity, 0.0001f, 0.0200f, "%.4f", ImGuiSliderFlags_Logarithmic);

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

    // SSR mode selector.
    {
        const char* ssrModes[] = {"Sharp (proximity fade)", "Stochastic (temporal)", "Masked (world-space fade)"};
        ImGui::Combo("SSR Mode", &ssrMode, ssrModes, 3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Sharp: deterministic rays, proximity fade near objects\n"
                              "Stochastic: jittered rays + temporal accumulation (softer)\n"
                              "Masked: deterministic rays, world-space distance fade (IBL fills contact zone)");
    }

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
            if (showPlayerState && registry.all_of<PlayerState>(entity)) {
                const auto& c = registry.get<PlayerState>(entity);
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
        localPlayer != entt::null && registry.all_of<Position, Velocity, InputSnapshot, PlayerState>(localPlayer);

    if (k_hasPlayer) {
        const auto& pos = registry.get<Position>(localPlayer).value;
        const auto& vel = registry.get<Velocity>(localPlayer).value;
        const auto& input = registry.get<InputSnapshot>(localPlayer);
        const auto& playerState = registry.get<PlayerState>(localPlayer);
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
        const auto& playerState = registry.get<PlayerState>(localPlayer);
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
        localPlayer != entt::null && registry.all_of<Position, Velocity, InputSnapshot, PlayerState>(localPlayer);

    if (!k_hasPlayer) {
        ImGui::TextDisabled("No local player.");
        ImGui::End();
        return;
    }

    const auto& vel = registry.get<Velocity>(localPlayer).value;
    const auto& input = registry.get<InputSnapshot>(localPlayer);
    const auto& playerState = registry.get<PlayerState>(localPlayer);
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

// Render Toggles window

void DebugUI::buildRenderTogglesUI(Renderer& renderer)
{
    if (!showRenderToggles)
        return;

    RenderToggles& t = renderer.toggles;

    ImGui::SetNextWindowPos({940.f, 10.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({280.f, 460.f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render Toggles", &showRenderToggles)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Toggle systems to profile FPS impact.");
    ImGui::TextDisabled("Unchecked = skipped entirely (zero cost).");
    ImGui::Separator();

    // Count enabled systems for the "all on / all off" buttons.
    bool* allFlags[] = {&t.sceneGeometry,
                        &t.pbrModels,
                        &t.entityModels,
                        &t.weaponViewmodel,
                        &t.skybox,
                        &t.shadows,
                        &t.ssao,
                        &t.bloom,
                        &t.ssr,
                        &t.volumetrics,
                        &t.tonemap,
                        &t.particles,
                        &t.sdfText};
    constexpr int k_flagCount = 13;

    if (ImGui::Button("All ON")) {
        for (int i = 0; i < k_flagCount; ++i)
            *allFlags[i] = true;
        renderer.aaMode = AAMode::SMAA_T2x;
    }
    ImGui::SameLine();
    if (ImGui::Button("All OFF")) {
        for (int i = 0; i < k_flagCount; ++i)
            *allFlags[i] = false;
        renderer.aaMode = AAMode::Off;
    }
    ImGui::SameLine();
    if (ImGui::Button("Only Post-FX OFF")) {
        t.ssao = false;
        t.bloom = false;
        t.ssr = false;
        t.volumetrics = false;
        renderer.aaMode = AAMode::Off;
    }
    ImGui::Separator();

    // Geometry
    ImGui::SeparatorText("Geometry");
    ImGui::Checkbox("Scene Geometry (cube+floor)", &t.sceneGeometry);
    ImGui::Checkbox("PBR Models (Wraith, Porsche, etc.)", &t.pbrModels);
    ImGui::Checkbox("Entity Models (ECS Renderable)", &t.entityModels);
    ImGui::Checkbox("Weapon Viewmodel (R-301)", &t.weaponViewmodel);
    ImGui::Checkbox("Skybox", &t.skybox);

    // Lighting
    ImGui::SeparatorText("Lighting / Shadows");
    ImGui::Checkbox("Shadow Map", &t.shadows);

    // Post-processing
    ImGui::SeparatorText("Post-Processing");
    ImGui::Checkbox("SSAO", &t.ssao);
    ImGui::Checkbox("Bloom", &t.bloom);
    ImGui::Checkbox("SSR (Screen-Space Reflections)", &t.ssr);
    ImGui::Checkbox("Volumetric Lighting", &t.volumetrics);

    // Anti-aliasing mode selection
    const char* aaModes[] = {"Off", "SMAA 1x", "SMAA T2x"};
    ImGui::Combo("Anti-Aliasing", reinterpret_cast<int*>(&renderer.aaMode), aaModes, 3);

    // CAS sharpening
    ImGui::Checkbox("CAS Sharpening", &renderer.casEnabled);
    if (renderer.casEnabled)
        ImGui::SliderFloat("CAS Strength", &renderer.casStrength, 0.0f, 1.0f, "%.2f");

    ImGui::Checkbox("Tone Mapping (HDR->LDR)", &t.tonemap);

    // Effects
    ImGui::SeparatorText("Effects");
    ImGui::Checkbox("Particle System", &t.particles);
    ImGui::Checkbox("SDF Text (HUD + World)", &t.sdfText);

    ImGui::End();
}

void DebugUI::buildLightingUI(Renderer& renderer)
{
    if (!showLightingControls)
        return;

    ImGui::SetNextWindowPos({1230.f, 10.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({300.f, 520.f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lighting Controls", &showLightingControls)) {
        ImGui::End();
        return;
    }

    // Sun position
    ImGui::SeparatorText("Sun Position");
    ImGui::SliderFloat("Azimuth", &renderer.sunAzimuth, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("Elevation", &renderer.sunElevation, 5.0f, 90.0f, "%.0f deg");

    // Light intensities
    ImGui::SeparatorText("Light Intensity");
    ImGui::SliderFloat("Sun", &renderer.sunIntensity, 0.0f, 10.0f, "%.1f");
    ImGui::SliderFloat("Fill", &renderer.fillIntensity, 0.0f, 3.0f, "%.2f");

    // Ambient
    ImGui::SeparatorText("Ambient");
    float amb[3] = {renderer.ambientR, renderer.ambientG, renderer.ambientB};
    if (ImGui::ColorEdit3("Ambient Color", amb)) {
        renderer.ambientR = amb[0];
        renderer.ambientG = amb[1];
        renderer.ambientB = amb[2];
    }

    // IBL intensity
    // Dial IBL Specular below 1.0 if dielectric surfaces (plastic, cloth,
    // skin) look like polished metal -- real-world HDR environments make
    // smooth dielectrics look artificially reflective.
    ImGui::SeparatorText("IBL Intensity");
    ImGui::SliderFloat("IBL Diffuse", &renderer.iblDiffuseIntensity, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("IBL Specular", &renderer.iblSpecularIntensity, 0.0f, 2.0f, "%.2f");

    // Post-processing
    ImGui::SeparatorText("Post-Processing");
    ImGui::SliderFloat("Bloom", &renderer.bloomStr, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("SSAO", &renderer.ssaoStr, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("AO Radius", &renderer.ssaoRadius, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("AO Falloff", &renderer.ssaoFalloff, 0.5f, 4.0f, "%.1f");
    ImGui::SliderFloat("AO Power", &renderer.ssaoPower, 0.5f, 3.0f, "%.1f");
    ImGui::SliderFloat("SSR", &renderer.ssrStr, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Volumetric", &renderer.volStr, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Sharpen", &renderer.sharpenStr, 0.0f, 2.0f, "%.2f");

    // Cascaded Shadow Maps
    ImGui::SeparatorText("Cascaded Shadows");
    ImGui::SliderFloat("Depth Bias", &renderer.shadowBiasVal, 0.0f, 0.01f, "%.5f");
    ImGui::SliderFloat("Normal Bias", &renderer.shadowNormalBiasVal, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Shadow Distance", &renderer.shadowDistance, 500.0f, 10000.0f, "%.0f");
    ImGui::SliderFloat("Cascade Lambda", &renderer.cascadeLambda, 0.0f, 1.0f, "%.2f");

    ImGui::End();
}

void DebugUI::buildSkyboxUI(Renderer& renderer)
{
    if (!showSkybox)
        return;

    ImGui::SetNextWindowPos({940.f, 480.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({280.f, 300.f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Skybox", &showSkybox)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Current: %s", renderer.currentHDRName.c_str());
    ImGui::Separator();

    if (ImGui::Button("Procedural Sky")) {
        renderer.useHDRSkybox = false;
        renderer.currentHDRName = "(procedural)";
    }
    ImGui::Separator();

    ImGui::Text("HDR Environments:");
    for (const auto& path : renderer.availableHDRFiles) {
        // Extract filename stem for display.
        auto stem = std::filesystem::path(path).stem().string();
        bool isCurrent = (renderer.useHDRSkybox && stem == renderer.currentHDRName);

        if (isCurrent)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));

        if (ImGui::Button(stem.c_str(), ImVec2(-1, 0))) {
            renderer.loadHDRSkybox(path);
        }

        if (isCurrent)
            ImGui::PopStyleColor();
    }

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

    ImGui::SetNextWindowPos({980.0f, 500.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({290.0f, 160.0f}, ImGuiCond_FirstUseEver);
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
    const GunInstance& gun = (weapon.current == WeaponSlot::PRIMARY) ? weapon.primary : weapon.secondary;

    const char* currentGunName = "?";
    switch (gun.type) {
    case WeaponType::Rifle:
        currentGunName = "Rifle";
        break;
    case WeaponType::Rocket:
        currentGunName = "Rocket";
        break;
    case WeaponType::RailGun:
        currentGunName = "RailGun";
        break;
    case WeaponType::EnergyGun:
        currentGunName = "EnergyGun";
        break;
    }

    ImGui::SeparatorText("Weapon");
    ImGui::Text("Current: %s", currentGunName);
    ImGui::Text("Ammo:    %d / %d", gun.currentMagAmmo, gun.totalAmmo);

    ImGui::SeparatorText("Vitals");
    if (registry.all_of<Health>(localPlayer)) {
        const Health& health = registry.get<Health>(localPlayer);
        ImGui::Text("Armor:   %.0f / %.0f", static_cast<double>(health.armor), static_cast<double>(systems::armorMax));
        ImGui::Text(
            "Health:  %.0f / %.0f", static_cast<double>(health.health), static_cast<double>(systems::healthMax));
    } else {
        ImGui::TextDisabled("Health state unavailable");
    }

    ImGui::End();
}

void DebugUI::buildScoreboardUI(const Registry& registry, MatchPhase phase, float countdownTimer)
{
    if (!showScoreboard_)
        return;

    // Find local player entity to highlight it in the table.
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

    ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags k_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Scoreboard", &showScoreboard_, k_flags)) {
        ImGui::End();
        return;
    }

    // Phase banner
    const char* phaseStr = "Warmup";
    switch (phase) {
    case MatchPhase::COUNTDOWN:
        phaseStr = "Starting...";
        break;
    case MatchPhase::IN_PROGRESS:
        phaseStr = "In Progress";
        break;
    case MatchPhase::FINISHED:
        phaseStr = "Finished";
        break;
    default:
        break;
    }
    ImGui::TextUnformatted(phaseStr);
    if (phase == MatchPhase::COUNTDOWN || phase == MatchPhase::FINISHED)
        ImGui::Text("%.1fs", static_cast<double>(countdownTimer));
    ImGui::Separator();

    constexpr ImGuiTableFlags k_tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("scores", 5, k_tableFlags)) {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("K", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Sc", ImGuiTableColumnFlags_WidthFixed, 35.0f);
        ImGui::TableSetupColumn("Won", ImGuiTableColumnFlags_WidthFixed, 35.0f);
        ImGui::TableHeadersRow();

        int row = 0;
        if (k_es) {
            for (auto e : *k_es) {
                if (!registry.valid(e) || !registry.all_of<PlayerMatchStats>(e))
                    continue;

                const auto& stats = registry.get<PlayerMatchStats>(e);
                const bool k_isLocal = (e == localPlayer);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (k_isLocal)
                    ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, "> You");
                else
                    ImGui::Text("Player %d", row + 1);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", stats.kills);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", stats.deaths);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", stats.score);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s", stats.hasWon ? "Yes" : "No");

                ++row;
            }
        }
        if (row == 0)
            ImGui::TextDisabled("No players");

        ImGui::EndTable();
    }

    ImGui::End();
}

void DebugUI::render()
{
    ImGui::Render();
}
