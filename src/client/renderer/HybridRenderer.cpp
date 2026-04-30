/// @file HybridRenderer.cpp
/// @brief Implementation of the legacy/new routing renderer.

#include "HybridRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdlib> // abort()

namespace
{

/// @brief Human-readable name for each renderer feature (for logging).
const char* featureName(RendererFeature f)
{
    switch (f) {
    case RendererFeature::Init:
        return "Init";
    case RendererFeature::DrawFrame:
        return "DrawFrame";
    case RendererFeature::Quit:
        return "Quit";
    case RendererFeature::GetDevice:
        return "GetDevice";
    case RendererFeature::GetShaderFormat:
        return "GetShaderFormat";
    case RendererFeature::GetCamera:
        return "GetCamera";
    case RendererFeature::SetParticleSystem:
        return "SetParticleSystem";
    case RendererFeature::LoadSceneModel:
        return "LoadSceneModel";
    case RendererFeature::UploadSceneModel:
        return "UploadSceneModel";
    case RendererFeature::SetVSync:
        return "SetVSync";
    case RendererFeature::UpdateModelMeshVertices:
        return "UpdateModelMeshVertices";
    case RendererFeature::SetEntityRenderList:
        return "SetEntityRenderList";
    case RendererFeature::SetPointLights:
        return "SetPointLights";
    case RendererFeature::SetWeaponViewmodel:
        return "SetWeaponViewmodel";
    case RendererFeature::RequestScreenshot:
        return "RequestScreenshot";
    case RendererFeature::ModelCount:
        return "ModelCount";
    case RendererFeature::SetHudTexture:
        return "SetHudTexture";
    }
    return "Unknown";
}

/// @brief All features whose routing is interesting to report at init.
constexpr RendererFeature k_allFeatures[] = {
    RendererFeature::Init,
    RendererFeature::DrawFrame,
    RendererFeature::Quit,
    RendererFeature::GetDevice,
    RendererFeature::GetShaderFormat,
    RendererFeature::GetCamera,
    RendererFeature::SetParticleSystem,
    RendererFeature::LoadSceneModel,
    RendererFeature::UploadSceneModel,
    RendererFeature::SetVSync,
    RendererFeature::UpdateModelMeshVertices,
    RendererFeature::SetEntityRenderList,
    RendererFeature::SetPointLights,
    RendererFeature::SetWeaponViewmodel,
    RendererFeature::RequestScreenshot,
    RendererFeature::ModelCount,
    RendererFeature::SetHudTexture,
};

} // namespace

HybridRenderer::HybridRenderer() : ssrMode(legacy_.ssrMode), toggles(legacy_.toggles) {}

bool HybridRenderer::supports(RendererFeature /*feature*/) const
{
    // From the outside the hybrid supports everything -- internally it routes.
    return true;
}

bool HybridRenderer::init(SDL_Window* window)
{
    SDL_Log("HybridRenderer: ==== INIT BEGIN ====");

    // The legacy renderer owns the GPU device + window claim + ImGui GPU
    // backend, because (a) it's fully functional and (b) most routed calls
    // still land on it. The new renderer piggy-backs on the same device.
    if (!legacy_.init(window)) {
        SDL_Log("HybridRenderer: FATAL -- legacy renderer init failed");
        return false;
    }
    legacyInitialised_ = true;
    SDL_Log("HybridRenderer: legacy renderer init OK");

    // If the new renderer claims DrawFrame or Init, it MUST initialise
    // successfully.  Silent fallback to legacy is not allowed -- if the new
    // renderer says it can do something, it has to actually work.
    if (next_.supports(RendererFeature::DrawFrame) || next_.supports(RendererFeature::Init)) {
        SDL_Log("HybridRenderer: new renderer claims DrawFrame/Init support -- attempting init...");
        if (!next_.init(window, legacy_.getDevice())) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER,
                            "HybridRenderer: FATAL -- new renderer claims DrawFrame support but init FAILED.\n"
                            "    Fix the new renderer or remove DrawFrame from NewRenderer::supports().\n"
                            "    Will NOT silently fall back to legacy.");
            std::abort();
        }
        nextInitialised_ = true;
        SDL_Log("HybridRenderer: new renderer init OK");
    } else {
        SDL_Log("HybridRenderer: new renderer does not claim DrawFrame or Init -- skipping init");
    }

    // Print the full routing table so it's unambiguous which renderer handles
    // each feature. This runs once at startup.
    SDL_Log("HybridRenderer: ---- ROUTING TABLE ----");
    for (RendererFeature f : k_allFeatures) {
        const bool newClaims = next_.supports(f);
        const bool useNew = nextInitialised_ && newClaims;
        SDL_Log("HybridRenderer:   %-25s -> %s%s",
                featureName(f),
                useNew ? "NEW" : "LEGACY",
                (newClaims && !nextInitialised_) ? "  (new claims support but init failed!)" : "");
    }
    SDL_Log("HybridRenderer: ==== INIT DONE (drawFrame -> %s) ====",
            (nextInitialised_ && next_.supports(RendererFeature::DrawFrame)) ? "NEW" : "LEGACY");

    return true;
}

void HybridRenderer::drawFrame(glm::vec3 eye, float yaw, float pitch, float roll)
{
    const bool useNew = nextInitialised_ && next_.supports(RendererFeature::DrawFrame);

    // Log once on the very first frame so the user sees it even without scrolling
    // back to init.
    if (!drawFrameLogged_) {
        SDL_Log("HybridRenderer::drawFrame: first frame -> %s renderer", useNew ? "NEW" : "LEGACY");
        drawFrameLogged_ = true;
    }

    if (useNew)
        next_.drawFrame(eye, yaw, pitch, roll);
    else
        legacy_.drawFrame(eye, yaw, pitch, roll);
}

void HybridRenderer::quit()
{
    // Tear down new first so its buffers release before the shared device dies.
    if (nextInitialised_) {
        SDL_Log("HybridRenderer: quitting new renderer");
        next_.quit();
        nextInitialised_ = false;
    }
    if (legacyInitialised_) {
        SDL_Log("HybridRenderer: quitting legacy renderer");
        legacy_.quit();
        legacyInitialised_ = false;
    }
}

SDL_GPUDevice* HybridRenderer::getDevice() const
{
    if (nextInitialised_ && next_.supports(RendererFeature::GetDevice))
        return next_.getDevice();
    return legacy_.getDevice();
}

SDL_GPUShaderFormat HybridRenderer::getShaderFormat() const
{
    if (nextInitialised_ && next_.supports(RendererFeature::GetShaderFormat))
        return next_.getShaderFormat();
    return legacy_.getShaderFormat();
}

const Camera& HybridRenderer::getCamera() const
{
    if (nextInitialised_ && next_.supports(RendererFeature::GetCamera))
        return next_.getCamera();
    return legacy_.getCamera();
}

void HybridRenderer::setParticleSystem(ParticleSystem* ps)
{
    if (next_.supports(RendererFeature::SetParticleSystem))
        next_.setParticleSystem(ps);
    else
        legacy_.setParticleSystem(ps);
}

int HybridRenderer::loadSceneModel(const char* filename, glm::vec3 pos, float scale, bool flipUVs)
{
    if (next_.supports(RendererFeature::LoadSceneModel))
        return next_.loadSceneModel(filename, pos, scale, flipUVs);
    return legacy_.loadSceneModel(filename, pos, scale, flipUVs);
}

int HybridRenderer::uploadSceneModel(const LoadedModel& model)
{
    if (next_.supports(RendererFeature::UploadSceneModel))
        return next_.uploadSceneModel(model);
    return legacy_.uploadSceneModel(model);
}

bool HybridRenderer::setVSync(bool enabled)
{
    if (next_.supports(RendererFeature::SetVSync))
        return next_.setVSync(enabled);
    return legacy_.setVSync(enabled);
}

void HybridRenderer::updateModelMeshVertices(int modelIndex,
                                             int meshIndex,
                                             const ModelVertex* vertices,
                                             Uint32 vertexCount)
{
    if (next_.supports(RendererFeature::UpdateModelMeshVertices))
        next_.updateModelMeshVertices(modelIndex, meshIndex, vertices, vertexCount);
    else
        legacy_.updateModelMeshVertices(modelIndex, meshIndex, vertices, vertexCount);
}

void HybridRenderer::setEntityRenderList(std::vector<EntityRenderCmd> cmds)
{
    if (next_.supports(RendererFeature::SetEntityRenderList))
        next_.setEntityRenderList(std::move(cmds));
    else
        legacy_.setEntityRenderList(std::move(cmds));
}

void HybridRenderer::setPointLights(std::vector<PointLight> lights)
{
    if (next_.supports(RendererFeature::SetPointLights))
        next_.setPointLights(std::move(lights));
    else
        legacy_.setPointLights(std::move(lights));
}

void HybridRenderer::setModelEmissive(int modelIndex, glm::vec4 emissiveFactor)
{
    // Always route to legacy — model indices are owned by the legacy renderer.
    legacy_.setModelEmissive(modelIndex, emissiveFactor);
}

void HybridRenderer::setModelScenePass(int modelIndex, bool drawInScene)
{
    // Always route to legacy — model indices are owned by the legacy renderer.
    legacy_.setModelScenePass(modelIndex, drawInScene);
}

void HybridRenderer::setWeaponViewmodel(const WeaponViewmodel& vm)
{
    if (next_.supports(RendererFeature::SetWeaponViewmodel))
        next_.setWeaponViewmodel(vm);
    else
        legacy_.setWeaponViewmodel(vm);
}

void HybridRenderer::requestScreenshot(const std::string& path)
{
    if (next_.supports(RendererFeature::RequestScreenshot))
        next_.requestScreenshot(path);
    else
        legacy_.requestScreenshot(path);
}

int HybridRenderer::modelCount() const
{
    if (next_.supports(RendererFeature::ModelCount))
        return next_.modelCount();
    return legacy_.modelCount();
}

void HybridRenderer::setHudTexture(SDL_GPUTexture* hudOutput)
{
    // Always route to both — whichever is doing drawFrame needs the texture.
    legacy_.setHudTexture(hudOutput);
    if (nextInitialised_)
        next_.setHudTexture(hudOutput);
}
