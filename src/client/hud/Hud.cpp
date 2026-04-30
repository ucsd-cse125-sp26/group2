/// @file Hud.cpp
/// @brief Top-level HUD orchestrator.

#include "Hud.hpp"

#include "particles/sdf/SdfAtlas.hpp"
#include "widgets/AmmoCounter.hpp"
#include "widgets/CrosshairWidget.hpp"
#include "widgets/HealthArmorBar.hpp"
#include "widgets/HitMarkerWidget.hpp"

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
    // Interactive widgets (buy menu, scoreboard) handle events here.
    // Iterate in reverse so topmost widget gets first chance.
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        if ((*it)->visible) {
            // Widgets that consume events will set a flag — for now, no-op.
        }
    }
}

void Hud::update(float dt, const HudGameState& state)
{
    tweens_.update(dt);
    for (auto& w : widgets_) {
        if (w->visible)
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

    // Flush any remaining clip span.
    if (!context_.vertices().empty()) {
        // If no clip spans were emitted (no pushClipRect calls),
        // create a single full-viewport span.
        if (context_.clipSpans().empty()) {
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

    outX = baseX + w.offsetX;
    outY = baseY + w.offsetY;
}

void Hud::createWidgets()
{
    // Widgets added in draw order (back to front).
    widgets_.push_back(std::make_unique<CrosshairWidget>());
    widgets_.push_back(std::make_unique<HealthArmorBar>());
    widgets_.push_back(std::make_unique<AmmoCounter>());
    widgets_.push_back(std::make_unique<HitMarkerWidget>());
}
