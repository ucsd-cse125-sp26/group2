/// @file Hud.cpp
/// @brief Top-level HUD orchestrator.

#include "Hud.hpp"

#include "config/InputBindings.hpp"
#include "particles/sdf/SdfAtlas.hpp"
#include "widgets/AbilitySelectionWidget.hpp"
#include "widgets/AmmoCounter.hpp"
#include "widgets/ChatWidget.hpp"
#include "widgets/CrosshairWidget.hpp"
#include "widgets/DamageAccumWidget.hpp"
#include "widgets/DamageIndicator.hpp"
#include "widgets/DamageNumberWidget.hpp"
#include "widgets/EnemyWorldHealthBar.hpp"
#include "widgets/EquipmentSlots.hpp"
#include "widgets/GrenadeRadialWidget.hpp"
#include "widgets/GrenadeSlotsWidget.hpp"
#include "widgets/HealthArmorBar.hpp"
#include "widgets/HitMarkerWidget.hpp"
#include "widgets/KillFeed.hpp"
#include "widgets/Minimap.hpp"
#include "widgets/PickupNotification.hpp"
#include "widgets/PickupPrompt.hpp"
#include "widgets/PrematchBanner.hpp"
#include "widgets/RailgunScopeWidget.hpp"
#include "widgets/Scoreboard.hpp"
#include "widgets/ShotgunPelletWidget.hpp"
#include "widgets/VignetteWidget.hpp"

bool Hud::init(SDL_GPUDevice* device,
               SDL_GPUShaderFormat shaderFormat,
               const SdfAtlas& sdfAtlas,
               uint32_t screenW,
               uint32_t screenH)
{
    screenW_ = static_cast<float>(screenW);
    screenH_ = static_cast<float>(screenH);

    if (!renderer_.init(device, shaderFormat, sdfAtlas, screenW, screenH))
        return false;

    context_.init(&sdfAtlas);
    createWidgets();

    SDL_Log("Hud: init OK (%ux%u, %zu widgets)", screenW, screenH, widgets_.size());
    return true;
}

void Hud::quit()
{
    widgets_.clear();
    renderer_.quit();
}

void Hud::resize(uint32_t newW, uint32_t newH)
{
    screenW_ = static_cast<float>(newW);
    screenH_ = static_cast<float>(newH);
    renderer_.resize(newW, newH);
}

void Hud::processEvent(const SDL_Event* event, const InputBindings* bindings)
{
    if (!event || !bindings)
        return;

    bool down = false;
    if (bindings->eventMatches(Action::Scoreboard, *event, down)) {
        for (auto& w : widgets_) {
            if (auto* sb = dynamic_cast<Scoreboard*>(w.get()))
                sb->setOpen(down);
        }
    }
}

void Hud::update(float dt, const HudGameState& state)
{
    tweens_.update(dt);
    const float scale = screenH_ / 1080.f;
    // Update ALL widgets (not just visible ones) so data stays fresh
    // when toggled on (e.g. Scoreboard on TAB shows current frame data).
    for (auto& w : widgets_) {
        w->uiScale_ = scale;
        w->update(dt, state, tweens_);
    }
}

void Hud::render()
{
    context_.beginFrame();

    for (auto& w : widgets_) {
        const bool forceVisible =
            debugRenderInactiveWidgets_ && dynamic_cast<const RailgunScopeWidget*>(w.get()) == nullptr;
        if (!w->visible && !forceVisible)
            continue;

        const bool originalVisible = w->visible;
        if (forceVisible)
            w->visible = true;

        float drawX = 0.f, drawY = 0.f;
        resolveAnchor(*w, drawX, drawY);
        const std::size_t widgetStartVertex = context_.vertices().size();
        w->draw(context_, drawX, drawY);
        context_.tintVertices(widgetStartVertex, w->tint);

        w->visible = originalVisible;
    }

    // Flush any remaining unflushed vertices (e.g. minimap drawn after last clip pop).
    context_.endFrame();

    if (!context_.vertices().empty()) {
        if (context_.clipSpans().empty()) {
            // No clipping was used — create a single full-viewport span.
            std::vector<std::array<float, 6>> fullSpan = {
                {0.f, static_cast<float>(context_.vertices().size()), 0.f, 0.f, -1.f, 0.f}};
            renderer_.render(context_.vertices(), fullSpan);
        } else {
            renderer_.render(context_.vertices(), context_.clipSpans());
        }
    }
}

void Hud::resolveAnchor(const HudWidget& w, float& outX, float& outY) const
{
    float baseX = 0.f, baseY = 0.f;

    switch (w.anchor) {
    case HudAnchor::TopLeft:
        baseX = 0.f;
        baseY = 0.f;
        break;
    case HudAnchor::TopCenter:
        baseX = screenW_ * 0.5f;
        baseY = 0.f;
        break;
    case HudAnchor::TopRight:
        baseX = screenW_;
        baseY = 0.f;
        break;
    case HudAnchor::CenterLeft:
        baseX = 0.f;
        baseY = screenH_ * 0.5f;
        break;
    case HudAnchor::Center:
        baseX = screenW_ * 0.5f;
        baseY = screenH_ * 0.5f;
        break;
    case HudAnchor::CenterRight:
        baseX = screenW_;
        baseY = screenH_ * 0.5f;
        break;
    case HudAnchor::BottomLeft:
        baseX = 0.f;
        baseY = screenH_;
        break;
    case HudAnchor::BottomCenter:
        baseX = screenW_ * 0.5f;
        baseY = screenH_;
        break;
    case HudAnchor::BottomRight:
        baseX = screenW_;
        baseY = screenH_;
        break;
    }

    outX = baseX + w.offsetX * w.uiScale_;
    outY = baseY + w.offsetY * w.uiScale_;
}

void Hud::createWidgets()
{
    // Widgets added in draw order (back to front).
    widgets_.push_back(std::make_unique<RailgunScopeWidget>());

    // Vignette goes first (full-screen overlay behind everything else).
    widgets_.push_back(std::make_unique<VignetteWidget>());

    // World-space markers — drawn on top of the scene but before screen-space
    // chrome so the chrome can occlude them at edges.
    widgets_.push_back(std::make_unique<EnemyWorldHealthBar>());
    widgets_.push_back(std::make_unique<DamageNumberWidget>());

    // Center reticle + hit-confirm + accumulated-damage stack.
    widgets_.push_back(std::make_unique<CrosshairWidget>());
    widgets_.push_back(std::make_unique<HitMarkerWidget>());
    widgets_.push_back(std::make_unique<ShotgunPelletWidget>());
    widgets_.push_back(std::make_unique<DamageAccumWidget>());

    // Directional damage arcs around the reticle.
    widgets_.push_back(std::make_unique<DamageIndicator>());
    widgets_.push_back(std::make_unique<AbilitySelectionWidget>());
    widgets_.push_back(std::make_unique<GrenadeRadialWidget>());

    // Top right: killfeed.
    widgets_.push_back(std::make_unique<KillFeed>());
    widgets_.push_back(std::make_unique<PickupNotification>());

    // Top left: minimap.
    widgets_.push_back(std::make_unique<Minimap>());

    // Bottom-row chrome.
    widgets_.push_back(std::make_unique<HealthArmorBar>()); // Vitals (bottom-left)
    widgets_.push_back(std::make_unique<EquipmentSlots>()); // bottom-center
    widgets_.push_back(std::make_unique<GrenadeSlotsWidget>());
    widgets_.push_back(std::make_unique<AmmoCounter>());    // weapon panel (bottom-right)
    widgets_.push_back(std::make_unique<ChatWidget>());     // chat should sit above gameplay chrome

    // Modal panels (only visible when toggled).
    // TeamStatusBar is intentionally omitted in the Voidfall design.
    widgets_.push_back(std::make_unique<Scoreboard>());
    widgets_.push_back(std::make_unique<PickupPrompt>());

    // Pre-match overlay (warmup waiting message + countdown integer).
    widgets_.push_back(std::make_unique<PrematchBanner>());
}
