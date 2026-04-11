#include "debug/DebugUI.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "particles/ParticleSystem.hpp"
#include "renderer/Renderer.hpp" // for RenderToggles

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

// ─── file-local helpers ──────────────────────────────────────────────────────

namespace
{

/// Draw a line with a filled triangular arrowhead at `end`.
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

// ─── DebugUI methods ─────────────────────────────────────────────────────────

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
    ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({480.0f, 700.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("ECS Inspector");

    // Key bindings reminder
    ImGui::TextDisabled("ESC: toggle mouse  |  Q: quit  |  F1: test packet");
    ImGui::Separator();

    // ── Settings ─────────────────────────────────────────────────────────────
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
        ImGui::SetTooltip("ON:  VSync on — fps locked to monitor refresh rate\n"
                          "OFF: VSync off — uncapped fps (may use mailbox present)");

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

    // ── Performance ───────────────────────────────────────────────────────────
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

    ImGui::End();

    if (showMovementChart)
        buildMovementChart(registry);
}

void DebugUI::buildMovementChart(const Registry& registry)
{
    // ── find local player ─────────────────────────────────────────────────────
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

    // ── window setup ──────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({500.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({430.0f, 470.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Movement Chart");

    // ── canvas geometry ───────────────────────────────────────────────────────
    // Reserve a square canvas; keep ~52 px below it for the stats line + legend.
    const ImVec2 k_cursor = ImGui::GetCursorScreenPos();
    const ImVec2 k_avail = ImGui::GetContentRegionAvail();
    const float k_side = std::max(std::min(k_avail.x, k_avail.y - 52.0f), 80.0f);
    const ImVec2 k_canvasP1 = {k_cursor.x + k_side, k_cursor.y + k_side};

    // The invisible button "consumes" the canvas area so ImGui lays out correctly.
    ImGui::InvisibleButton("##mvmap", {k_side, k_side});
    ImDrawList* const dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(k_cursor, k_canvasP1, true);

    // ── coordinate helpers ────────────────────────────────────────────────────
    // World space:  ±1 500 units in X and Z, centred at the spawn origin.
    // Chart axes:   world -X  → screen right  (camera right = world -X)
    //               world +Z  → screen up     (flip, because screen Y grows downward)
    constexpr float k_worldHalf = 1500.0f;
    const float k_posScale = k_side / (2.0f * k_worldHalf); // px per world unit

    // Velocity vectors use a separate scale so they're always clearly visible:
    // k_maxGroundSpeed maps to 28 % of the canvas half-width.
    const float k_velScale = (k_side * 0.28f) / physics::k_maxGroundSpeed;

    const auto worldToScreen = [&](float wx, float wz) -> ImVec2 {
        return {k_cursor.x + k_side * 0.5f - wx * k_posScale, // negate X: world -X = screen right
                k_cursor.y + k_side * 0.5f - wz * k_posScale};
    };

    // ── background ────────────────────────────────────────────────────────────
    dl->AddRectFilled(k_cursor, k_canvasP1, IM_COL32(14, 14, 22, 255));

    // ── grid lines (every 500 world units) ───────────────────────────────────
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

    // ── player ────────────────────────────────────────────────────────────────
    const bool k_hasPlayer =
        localPlayer != entt::null && registry.all_of<Position, Velocity, InputSnapshot>(localPlayer);

    if (k_hasPlayer) {
        const auto& pos = registry.get<Position>(localPlayer).value;
        const auto& vel = registry.get<Velocity>(localPlayer).value;
        const auto& input = registry.get<InputSnapshot>(localPlayer);
        const bool grounded =
            registry.all_of<PlayerState>(localPlayer) && registry.get<PlayerState>(localPlayer).grounded;

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
            const float wishSpeed = grounded ? physics::k_maxGroundSpeed : physics::k_airMaxSpeed;
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

    // ── stats line ────────────────────────────────────────────────────────────
    ImGui::Spacing();
    if (k_hasPlayer) {
        const auto& vel = registry.get<Velocity>(localPlayer).value;
        const auto& input = registry.get<InputSnapshot>(localPlayer);
        const bool grounded =
            registry.all_of<PlayerState>(localPlayer) && registry.get<PlayerState>(localPlayer).grounded;
        const float hSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        const glm::vec3 wishDir =
            physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
        const float wishSpeed = grounded ? physics::k_maxGroundSpeed : physics::k_airMaxSpeed;
        const float wishMag = (wishDir.x != 0.0f || wishDir.z != 0.0f) ? wishSpeed : 0.0f;

        ImGui::Text("XZ: %5.0f  |  Y: %+6.1f  |  Wish: %5.0f  |  %s",
                    static_cast<double>(hSpeed),
                    static_cast<double>(vel.y),
                    static_cast<double>(wishMag),
                    grounded ? "GROUND" : "AIR");
    } else {
        ImGui::TextDisabled("--");
    }

    // ── legend ────────────────────────────────────────────────────────────────
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

// ─── Particle System debug/control window ─────────────────────────────────────

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

    // ── Status ────────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Status");
    if (ps.sdfReady())
        ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f}, "SDF Font: LOADED");
    else
        ImGui::TextColored({1.f, 0.5f, 0.3f, 1.f}, "SDF Font: not loaded (text rendering disabled)");

    // ── Live counts ───────────────────────────────────────────────────────────
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

    // ── Spawn Controls ────────────────────────────────────────────────────────
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
        ps.spawnHitscanBeam(hipfireOrigin, hitPoint, WeaponType::EnergyRifle);
        ps.spawnImpactEffect(hitPoint, wallNorm, SurfaceType::Energy, WeaponType::EnergyRifle);
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

// ─── Render Toggles window ────────────────────────────────────────────────────

void DebugUI::buildRenderTogglesUI(RenderToggles& t)
{
    ImGui::SetNextWindowPos({940.f, 10.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({280.f, 460.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Render Toggles");

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
                        &t.taa,
                        &t.tonemap,
                        &t.particles,
                        &t.sdfText};
    constexpr int k_flagCount = 14;

    if (ImGui::Button("All ON")) {
        for (int i = 0; i < k_flagCount; ++i)
            *allFlags[i] = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("All OFF")) {
        for (int i = 0; i < k_flagCount; ++i)
            *allFlags[i] = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Only Post-FX OFF")) {
        t.ssao = false;
        t.bloom = false;
        t.ssr = false;
        t.volumetrics = false;
        t.taa = false;
    }
    ImGui::Separator();

    // ── Geometry ────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Geometry");
    ImGui::Checkbox("Scene Geometry (cube+floor)", &t.sceneGeometry);
    ImGui::Checkbox("PBR Models (Wraith, Porsche, etc.)", &t.pbrModels);
    ImGui::Checkbox("Entity Models (ECS Renderable)", &t.entityModels);
    ImGui::Checkbox("Weapon Viewmodel (R-301)", &t.weaponViewmodel);
    ImGui::Checkbox("Skybox", &t.skybox);

    // ── Lighting ────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Lighting / Shadows");
    ImGui::Checkbox("Shadow Map", &t.shadows);

    // ── Post-processing ─────────────────────────────────────────────────────
    ImGui::SeparatorText("Post-Processing");
    ImGui::Checkbox("SSAO", &t.ssao);
    ImGui::Checkbox("Bloom", &t.bloom);
    ImGui::Checkbox("SSR (Screen-Space Reflections)", &t.ssr);
    ImGui::Checkbox("Volumetric Lighting", &t.volumetrics);
    ImGui::Checkbox("TAA (Temporal AA)", &t.taa);
    ImGui::Checkbox("Tone Mapping (HDR->LDR)", &t.tonemap);

    // ── Effects ─────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Effects");
    ImGui::Checkbox("Particle System", &t.particles);
    ImGui::Checkbox("SDF Text (HUD + World)", &t.sdfText);

    ImGui::End();
}

void DebugUI::render()
{
    ImGui::Render();
}
