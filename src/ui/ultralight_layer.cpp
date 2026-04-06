#include "ultralight_layer.hpp"

#include "sdl3gpu_driver.hpp"

#include <SDL3/SDL.h>

#include <Ultralight/platform/Config.h>
#include <Ultralight/platform/FileSystem.h>
#include <Ultralight/platform/Logger.h>
#include <Ultralight/platform/Platform.h>

// ---------------------------------------------------------------------------
// Minimal Logger — routes Ultralight log messages to SDL_Log
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
// Minimal FileSystem — stub that always returns "file not found"
// ---------------------------------------------------------------------------
class SDLFileSystem : public ultralight::FileSystem
{
public:
    bool FileExists(const ultralight::String& path) override
    {
        (void)path;
        return false;
    }
    ultralight::String GetFileMimeType(const ultralight::String& path) override
    {
        (void)path;
        return {};
    }
    ultralight::String GetFileCharset(const ultralight::String& path) override
    {
        (void)path;
        return {};
    }
    ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& path) override
    {
        (void)path;
        return {};
    }
};
static SDLFileSystem gFileSystem;

// ---------------------------------------------------------------------------
// HTML menu content
// ---------------------------------------------------------------------------
static constexpr const char* k_menuHtml = R"HTML(
<html><head><style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { background: transparent; font-family: sans-serif; }
.menu {
    position: fixed; top: 50%; left: 50%;
    transform: translate(-50%, -50%);
    background: rgba(0,0,0,0.82);
    border: 1px solid rgba(255,255,255,0.12);
    border-radius: 10px;
    padding: 36px 52px;
    color: #fff; text-align: center; min-width: 260px;
}
h1 { font-size: 2.2em; letter-spacing: 0.12em; margin-bottom: 28px; text-transform: uppercase; }
button {
    display: block; width: 100%; margin: 10px 0;
    padding: 11px 0; background: rgba(255,255,255,0.08);
    border: 1px solid rgba(255,255,255,0.18);
    color: #fff; font-size: 1em; border-radius: 5px; cursor: pointer;
}
button:hover { background: rgba(255,255,255,0.2); }
</style></head>
<body><div class="menu">
<h1>TitanDoom</h1>
<button>New Game</button>
<button>Settings</button>
<button>Quit</button>
</div></body></html>
)HTML";

// ---------------------------------------------------------------------------
// UltralightLayer
// ---------------------------------------------------------------------------
UltralightLayer::UltralightLayer(SDL3GPUDriver* drv) : driver(drv) {}

UltralightLayer::~UltralightLayer()
{
    shutdown();
}

bool UltralightLayer::init(SDL_Window* window, SDL_GPUDevice* dev)
{
    gpuDevice = dev;

    ultralight::Config cfg;
    cfg.resource_path_prefix = "./resources/";
    cfg.face_winding = ultralight::FaceWinding::CounterClockwise;

    auto& platform = ultralight::Platform::instance();
    platform.set_config(cfg);
    platform.set_logger(&gLogger);
    platform.set_file_system(&gFileSystem);
    platform.set_gpu_driver(driver);

    renderer = ultralight::Renderer::Create();
    if (!renderer) {
        SDL_Log("[UL] Renderer::Create failed");
        return false;
    }

    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);

    ultralight::ViewConfig vc;
    vc.is_accelerated = true;
    vc.is_transparent = true;
    vc.initial_device_scale = 1.0;

    view = renderer->CreateView(static_cast<uint32_t>(w), static_cast<uint32_t>(h), vc, nullptr);
    if (!view) {
        SDL_Log("[UL] CreateView failed");
        return false;
    }

    view->LoadHTML(k_menuHtml);
    SDL_Log("[UL] View created (%d×%d), HTML loaded", w, h);
    return true;
}

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

void UltralightLayer::composite(SDL_GPURenderPass* pass, uint32_t /*vpW*/, uint32_t /*vpH*/)
{
    if (!view)
        return;

    const ultralight::RenderTarget rt = view->render_target();
    SDL_GPUTexture* ulTex = driver->getTexture(rt.texture_id);
    SDL_GPUSampler* ulSamp = driver->getSampler(rt.texture_id);
    if (!ulTex || !ulSamp)
        return;

    SDL_BindGPUGraphicsPipeline(pass, driver->getCompositePipeline());

    SDL_GPUTextureSamplerBinding b{};
    b.texture = ulTex;
    b.sampler = ulSamp;
    SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

void UltralightLayer::shutdown()
{
    view = nullptr;
    renderer = nullptr;
}
