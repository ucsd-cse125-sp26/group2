#include "ultralight_layer.hpp"

#include "sdl3gpu_driver.hpp"

#include <SDL3/SDL.h>

#include <AppCore/JSHelpers.h>
#include <AppCore/Platform.h>
#include <Ultralight/Buffer.h>
#include <Ultralight/MouseEvent.h>
#include <Ultralight/ScrollEvent.h>
#include <Ultralight/platform/Config.h>
#include <Ultralight/platform/FileSystem.h>
#include <Ultralight/platform/Logger.h>
#include <Ultralight/platform/Platform.h>
#include <string>

// ---------------------------------------------------------------------------
// Logger — routes Ultralight log messages to SDL_Log
// ---------------------------------------------------------------------------
class SDLLogger : public ultralight::Logger
{
public:
    void LogMessage(ultralight::LogLevel level, const ultralight::String& msg) override
    {
        const char* tag = (level == ultralight::LogLevel::Error)     ? "[UL ERROR]"
                          : (level == ultralight::LogLevel::Warning) ? "[UL WARN]"
                                                                     : "[UL]";
        SDL_Log("%s %s", tag, msg.utf8().data());
    }
};
static SDLLogger gLogger;

// ---------------------------------------------------------------------------
// FileSystem — reads files from the real filesystem via SDL I/O.
// ---------------------------------------------------------------------------
class SDLFileSystem : public ultralight::FileSystem
{
public:
    bool FileExists(const ultralight::String& path) override
    {
        SDL_PathInfo info{};
        return SDL_GetPathInfo(path.utf8().data(), &info);
    }

    ultralight::String GetFileMimeType(const ultralight::String& /*path*/) override
    {
        return "application/octet-stream";
    }

    ultralight::String GetFileCharset(const ultralight::String& /*path*/) override { return "utf-8"; }

    ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& path) override
    {
        SDL_IOStream* io = SDL_IOFromFile(path.utf8().data(), "rb");
        if (!io)
            return {};
        const Sint64 k_fileSize = SDL_GetIOSize(io);
        if (k_fileSize <= 0) {
            SDL_CloseIO(io);
            return {};
        }
        void* buf = SDL_malloc(static_cast<size_t>(k_fileSize));
        if (!buf) {
            SDL_CloseIO(io);
            return {};
        }
        if (SDL_ReadIO(io, buf, static_cast<size_t>(k_fileSize)) != static_cast<size_t>(k_fileSize)) {
            SDL_free(buf);
            SDL_CloseIO(io);
            return {};
        }
        SDL_CloseIO(io);
        return ultralight::Buffer::Create(
            buf, static_cast<size_t>(k_fileSize), buf, [](void* userData, void* /*data*/) { SDL_free(userData); });
    }
};
static SDLFileSystem gFileSystem;

// ---------------------------------------------------------------------------
// HTML/CSS/JS content loaded into the view
// ---------------------------------------------------------------------------
static constexpr const char* k_menuHtml = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    background: transparent;
    font-family: 'Segoe UI', system-ui, sans-serif;
    display: flex;
    align-items: center;
    justify-content: center;
    height: 100vh;
    color: #fff;
    user-select: none;
}
.menu {
    background: rgba(8, 10, 22, 0.88);
    border: 1px solid rgba(96, 165, 250, 0.2);
    border-radius: 14px;
    padding: 40px 56px 32px;
    text-align: center;
    min-width: 280px;
    box-shadow: 0 16px 48px rgba(0, 0, 0, 0.6), 0 0 0 1px rgba(255,255,255,0.04) inset;
}
h1 {
    font-size: 2.2em;
    letter-spacing: 0.18em;
    text-transform: uppercase;
    color: #93c5fd;
    margin-bottom: 4px;
}
.version {
    font-size: 0.7em;
    letter-spacing: 0.1em;
    color: rgba(255,255,255,0.25);
    margin-bottom: 28px;
}
.btn {
    display: block;
    width: 100%;
    margin: 8px 0;
    padding: 11px 0;
    background: rgba(255,255,255,0.05);
    border: 1px solid rgba(255,255,255,0.12);
    color: rgba(255,255,255,0.85);
    font-size: 0.92em;
    border-radius: 7px;
    cursor: pointer;
    letter-spacing: 0.04em;
    /* No CSS transform transitions — they force Ultralight to create GPU
       compositing sub-layers whose UV conventions conflict with our
       Vulkan flip_y=true setup, causing a visible 180-degree flip artifact. */
    transition: background 0.12s, border-color 0.12s;
    position: relative;
    padding-left: 2.2em;
    text-align: left;
}
.btn:hover {
    background: rgba(96, 165, 250, 0.18);
    border-color: rgba(96, 165, 250, 0.45);
    color: #fff;
}
.btn:active {
    background: rgba(96, 165, 250, 0.3);
    border-color: rgba(96, 165, 250, 0.65);
}
/* CSS-only icons in ::before — no Unicode symbol font required. */
.btn::before {
    position: absolute;
    left: 0.85em;
    top: 50%;
}
/* Play: right-pointing triangle via CSS borders (no font glyph needed). */
.btn-play::before {
    content: '';
    display: block;
    width: 0;
    height: 0;
    border-top: 0.38em solid transparent;
    border-bottom: 0.38em solid transparent;
    border-left: 0.55em solid rgba(147,197,253,0.75);
    margin-top: -0.38em;
}
/* Settings: small hollow circle. */
.btn-settings::before {
    content: '';
    display: block;
    width: 0.72em;
    height: 0.72em;
    border-radius: 50%;
    border: 2px solid rgba(147,197,253,0.75);
    margin-top: -0.36em;
    box-sizing: border-box;
}
/* Quit: multiplication sign U+00D7 (Latin-1 Supplement — present in
   every font, including DejaVu/Liberation/Ubuntu on Linux). */
.btn-quit::before {
    content: '\D7';
    font-size: 1.1em;
    color: rgba(147,197,253,0.75);
    line-height: 1;
    margin-top: -0.55em;
}
.divider {
    height: 1px;
    background: rgba(255,255,255,0.07);
    margin: 16px 0;
}
.status-box {
    background: rgba(0,0,0,0.35);
    border: 1px solid rgba(255,255,255,0.06);
    border-radius: 8px;
    padding: 10px 14px;
    font-size: 0.72em;
    font-family: 'Courier New', monospace;
    color: rgba(96,210,150,0.85);
    text-align: left;
    min-height: 40px;
    word-break: break-all;
}
.footer {
    margin-top: 12px;
    font-size: 0.65em;
    color: rgba(255,255,255,0.18);
    letter-spacing: 0.06em;
}
</style>
</head>
<body>
<div class="menu">
    <h1>TitanDoom</h1>
    <div class="version">v0.1.0-alpha</div>

    <button class="btn btn-play"     onclick="action('new_game')">New Game</button>
    <button class="btn btn-settings" onclick="action('settings')">Settings</button>
    <button class="btn btn-quit"     onclick="action('quit')">Quit</button>

    <div class="divider"></div>

    <div class="status-box" id="status">Waiting for C++ bridge&hellip;</div>
    <div class="footer" id="footer">frames: –</div>
</div>

<script>
// ---- JS → C++ ----
// Sends a named action to the C++ host.
// window.onAction is bound by C++ in OnDOMReady.
function action(name) {
    var el = document.getElementById('status');
    el.textContent = '→ C++: action("' + name + '")';
    if (typeof window.onAction === 'function') {
        window.onAction(name);
    }
}

// ---- C++ → JS API ----
// These are called by C++ via view->EvaluateScript().

window.setStatus = function(msg) {
    document.getElementById('status').textContent = msg;
};

window.setFooter = function(msg) {
    document.getElementById('footer').textContent = msg;
};

// Signal to C++ that the page is ready (C++ polls via EvaluateScript too)
console.log('[JS] DOM ready, waiting for C++ bridge setup.');
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// UltralightLayer
// ---------------------------------------------------------------------------
UltralightLayer::UltralightLayer(SDL3GPUDriver* drv) : driver(drv) {}

UltralightLayer::~UltralightLayer()
{
    shutdown();
}

bool UltralightLayer::init(SDL_Window* win, SDL_GPUDevice* dev)
{
    gpuDevice = dev;
    sdlWindow = win;

    const char* rawBase = SDL_GetBasePath();
    const std::string k_resourcePrefix = std::string(rawBase ? rawBase : "./") + "resources/";

    ultralight::Config cfg;
    cfg.resource_path_prefix = k_resourcePrefix.c_str();
    cfg.face_winding = ultralight::FaceWinding::CounterClockwise;

    auto& platform = ultralight::Platform::instance();
    platform.set_config(cfg);
    platform.set_logger(&gLogger);
    platform.set_font_loader(ultralight::GetPlatformFontLoader());
    platform.set_file_system(&gFileSystem);
    platform.set_gpu_driver(driver);

    renderer = ultralight::Renderer::Create();
    if (!renderer) {
        SDL_Log("[UL] Renderer::Create failed");
        return false;
    }

    int w = 0, h = 0;
    SDL_GetWindowSize(win, &w, &h);

    ultralight::ViewConfig vc;
    vc.is_accelerated = true;
    vc.is_transparent = true;
    vc.initial_device_scale = 1.0;

    view = renderer->CreateView(static_cast<uint32_t>(w), static_cast<uint32_t>(h), vc, nullptr);
    if (!view) {
        SDL_Log("[UL] CreateView failed");
        return false;
    }

    // Register ourselves as the load listener so OnDOMReady fires.
    view->set_load_listener(this);

    view->LoadHTML(k_menuHtml);
    SDL_Log("[UL] View created (%d×%d), HTML loaded", w, h);
    return true;
}

void UltralightLayer::setActionCallback(ActionCallback cb)
{
    actionCb = std::move(cb);
}

// ---------------------------------------------------------------------------
// LoadListener — OnDOMReady
// Called after the DOM has been built and JS objects are initialised.
// This is the right place to bind C++ functions into the JS global.
// ---------------------------------------------------------------------------
void UltralightLayer::OnDOMReady(ultralight::View* caller,
                                 uint64_t /*frameId*/,
                                 bool isMainFrame,
                                 const ultralight::String& /*url*/)
{
    if (!isMainFrame)
        return;

    // BindJSCallback macro uses unqualified JSCallback — bring it into scope.
    using ultralight::JSCallback;

    // Lock the JS context for this view.  The lock is released when the
    // RefPtr goes out of scope.
    auto ctx = caller->LockJSContext();
    ultralight::SetJSContext(ctx->ctx());

    // Expose window.onAction as a C++ function.
    // JS calls: window.onAction("new_game") etc.
    ultralight::JSObject global = ultralight::JSGlobalObject();
    global["onAction"] = BindJSCallback(&UltralightLayer::jsOnAction);

    // Tell the page the bridge is live.
    ultralight::JSEval("window.setStatus('C++ bridge ready — click a button!')");

    SDL_Log("[UL] JS bridge ready: window.onAction bound.");
}

// ---------------------------------------------------------------------------
// JS → C++ callback: called when JS executes window.onAction(name)
// ---------------------------------------------------------------------------
void UltralightLayer::jsOnAction(const ultralight::JSObject& /*thisObj*/, const ultralight::JSArgs& args)
{
    std::string action = "unknown";
    if (!args.empty()) {
        ultralight::String s = args[0].ToString();
        action = s.utf8().data();
    }

    SDL_Log("[UL] JS → C++ action: \"%s\"", action.c_str());

    if (actionCb)
        actionCb(action);
}

// ---------------------------------------------------------------------------
// Per-frame lifecycle
// ---------------------------------------------------------------------------
void UltralightLayer::update()
{
    if (renderer)
        renderer->Update();
}

void UltralightLayer::render()
{
    if (renderer)
        renderer->Render();
}

void UltralightLayer::composite(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, uint32_t /*vpW*/, uint32_t /*vpH*/)
{
    if (!view)
        return;

    ultralight::RenderTarget rt = view->render_target();
    if (rt.is_empty)
        return;

    SDL_GPUTexture* ulTex = driver->getTexture(rt.texture_id);
    SDL_GPUSampler* ulSamp = driver->getSampler(rt.texture_id);
    if (!ulTex || !ulSamp)
        return;

    SDL_BindGPUGraphicsPipeline(pass, driver->getCompositePipeline());

    SDL_GPUTextureSamplerBinding b{};
    b.texture = ulTex;
    b.sampler = ulSamp;
    SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);

    struct CompositeUniforms
    {
        float left, top, right, bottom;
    };
    CompositeUniforms uvRect{
        .left = rt.uv_coords.left,
        .top = rt.uv_coords.top,
        .right = rt.uv_coords.right,
        .bottom = rt.uv_coords.bottom,
    };
    SDL_PushGPUFragmentUniformData(cmd, 0, &uvRect, sizeof(uvRect));

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// Input forwarding
// ---------------------------------------------------------------------------
void UltralightLayer::fireMouseMove(int x, int y)
{
    if (!view)
        return;
    ultralight::MouseEvent evt{};
    evt.type = ultralight::MouseEvent::kType_MouseMoved;
    evt.x = x;
    evt.y = y;
    evt.button = ultralight::MouseEvent::kButton_None;
    view->FireMouseEvent(evt);
}

void UltralightLayer::fireMouseButton(int x, int y, bool down, bool rightButton)
{
    if (!view)
        return;
    ultralight::MouseEvent evt{};
    evt.type = down ? ultralight::MouseEvent::kType_MouseDown : ultralight::MouseEvent::kType_MouseUp;
    evt.x = x;
    evt.y = y;
    evt.button = rightButton ? ultralight::MouseEvent::kButton_Right : ultralight::MouseEvent::kButton_Left;
    view->FireMouseEvent(evt);
}

void UltralightLayer::fireScroll(int /*x*/, int /*y*/, int deltaX, int deltaY)
{
    if (!view)
        return;
    ultralight::ScrollEvent evt{};
    evt.type = ultralight::ScrollEvent::kType_ScrollByPixel;
    evt.delta_x = deltaX;
    evt.delta_y = deltaY;
    view->FireScrollEvent(evt);
}

// ---------------------------------------------------------------------------
// C++ → JS helpers
// ---------------------------------------------------------------------------
void UltralightLayer::evaluateScript(const char* js)
{
    if (!view)
        return;
    view->EvaluateScript(ultralight::String(js));
}

void UltralightLayer::pushStatus(const std::string& msg)
{
    // Escape single quotes so the string can safely go inside JS quotes.
    std::string safe;
    safe.reserve(msg.size());
    for (char c : msg) {
        if (c == '\'')
            safe += "\\'";
        else if (c == '\\')
            safe += "\\\\";
        else
            safe += c;
    }
    const std::string k_js = "if(window.setStatus) window.setStatus('" + safe + "')";
    evaluateScript(k_js.c_str());
}

void UltralightLayer::pushFrameCount(int n)
{
    char buf[64];
    SDL_snprintf(buf, sizeof(buf), "if(window.setFooter) window.setFooter('frames: %d')", n);
    evaluateScript(buf);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void UltralightLayer::shutdown()
{
    if (view)
        view->set_load_listener(nullptr);
    view = nullptr;
    renderer = nullptr;
}
