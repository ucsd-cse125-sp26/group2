#pragma once
#include <SDL3/SDL.h>

#include <AppCore/JSHelpers.h>
#include <Ultralight/Ultralight.h>
#include <functional>
#include <string>

class SDL3GPUDriver;

// ---------------------------------------------------------------------------
// UltralightLayer
//
// Owns the Ultralight Renderer + View and bridges it to the SDL3 GPU backend.
//
// JS ↔ C++ contract exposed to the loaded page
// --------------------------------------------------
// JS → C++:  window.onAction(name)  fires ActionCallback with the name string
//             (e.g. "new_game", "settings", "quit")
//
// C++ → JS:  evaluateScript(js)  runs arbitrary JS in the page context.
//             Convenience helpers: pushStatus(msg), pushFrameCount(n).
// ---------------------------------------------------------------------------
class UltralightLayer : public ultralight::LoadListener
{
public:
    // Callback the application provides to handle menu actions from JS.
    using ActionCallback = std::function<void(const std::string& action)>;

    explicit UltralightLayer(SDL3GPUDriver* driver);
    ~UltralightLayer() override;

    bool init(SDL_Window* window, SDL_GPUDevice* device);
    void setActionCallback(ActionCallback cb);

    // Per-frame lifecycle
    void update(); // call renderer->Update()
    void render(); // call renderer->Render()

    // Composite the Ultralight RTT into the current SDL render pass.
    void composite(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, uint32_t viewportW, uint32_t viewportH);

    // --- Input forwarding (call these from your SDL event loop) ---
    void fireMouseMove(int x, int y);
    void fireMouseButton(int x, int y, bool down, bool rightButton = false);
    void fireScroll(int x, int y, int deltaX, int deltaY);

    // --- C++ → JS helpers ---
    // Evaluate arbitrary JavaScript in the view's context (no-op if view is null).
    void evaluateScript(const char* js);

    // Convenience: call window.setStatus(msg) in the page.
    void pushStatus(const std::string& msg);

    // Convenience: call window.setFrameCount(n) in the page.
    void pushFrameCount(int n);

    void shutdown();

    // --- ultralight::LoadListener ---
    // Called when the DOM is ready and JS can be safely bound.
    void
    OnDOMReady(ultralight::View* caller, uint64_t frameId, bool isMainFrame, const ultralight::String& url) override;

private:
    // C++ callback invoked when JS calls window.onAction(name).
    void jsOnAction(const ultralight::JSObject& /*thisObj*/, const ultralight::JSArgs& args);

    SDL3GPUDriver* driver = nullptr;
    SDL_GPUDevice* gpuDevice = nullptr;
    SDL_Window* sdlWindow = nullptr;

    ultralight::RefPtr<ultralight::Renderer> renderer;
    ultralight::RefPtr<ultralight::View> view;

    ActionCallback actionCb;
};
