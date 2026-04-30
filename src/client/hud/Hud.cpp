/// @file Hud.cpp
/// @brief Top-level HUD orchestrator.

#include "Hud.hpp"

#include "particles/sdf/SdfAtlas.hpp"
#include "widgets/AmmoCounter.hpp"
#include "widgets/BuyMenu.hpp"
#include "widgets/CrosshairWidget.hpp"
#include "widgets/DamageAccumWidget.hpp"
#include "widgets/DamageIndicator.hpp"
#include "widgets/DamageNumberWidget.hpp"
#include "widgets/HealthArmorBar.hpp"
#include "widgets/HitMarkerWidget.hpp"
#include "widgets/KillFeed.hpp"
#include "widgets/Minimap.hpp"
#include "widgets/RoundTimer.hpp"
#include "widgets/Scoreboard.hpp"
#include "widgets/TeamStatusBar.hpp"
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

void Hud::processEvent(const SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_TAB) {
            for (auto& w : widgets_) {
                if (auto* sb = dynamic_cast<Scoreboard*>(w.get()))
                    sb->setOpen(true);
            }
        }
        if (event->key.key == SDLK_B) {
            for (auto& w : widgets_) {
                if (auto* bm = dynamic_cast<BuyMenu*>(w.get()))
                    bm->toggle(true);
            }
        }
    } else if (event->type == SDL_EVENT_KEY_UP) {
        if (event->key.key == SDLK_TAB) {
            for (auto& w : widgets_) {
                if (auto* sb = dynamic_cast<Scoreboard*>(w.get()))
                    sb->setOpen(false);
            }
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
        if (!w->visible)
            continue;
        float drawX = 0.f, drawY = 0.f;
        resolveAnchor(*w, drawX, drawY);
        w->draw(context_, drawX, drawY);
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
    // Vignette goes first (behind everything) since it's a full-screen overlay.
    widgets_.push_back(std::make_unique<VignetteWidget>());
    widgets_.push_back(std::make_unique<CrosshairWidget>());
    widgets_.push_back(std::make_unique<HealthArmorBar>());
    widgets_.push_back(std::make_unique<AmmoCounter>());
    widgets_.push_back(std::make_unique<HitMarkerWidget>());
    widgets_.push_back(std::make_unique<DamageNumberWidget>());
    widgets_.push_back(std::make_unique<DamageAccumWidget>());
    widgets_.push_back(std::make_unique<KillFeed>());
    widgets_.push_back(std::make_unique<DamageIndicator>());
    widgets_.push_back(std::make_unique<RoundTimer>());
    widgets_.push_back(std::make_unique<TeamStatusBar>());
    widgets_.push_back(std::make_unique<Scoreboard>());
    widgets_.push_back(std::make_unique<BuyMenu>());
    widgets_.push_back(std::make_unique<Minimap>());
}
