/// @file HybridRenderer.cpp
/// @brief Implementation of the legacy/new routing renderer.

#include "HybridRenderer.hpp"

#include <SDL3/SDL.h>

HybridRenderer::HybridRenderer() : ssrMode(legacy_.ssrMode), toggles(legacy_.toggles) {}

bool HybridRenderer::supports(RendererFeature /*feature*/) const
{
    // From the outside the hybrid supports everything -- internally it routes.
    return true;
}

bool HybridRenderer::init(SDL_Window* window)
{
    // The legacy renderer owns the GPU device + window claim + ImGui GPU
    // backend, because (a) it's fully functional and (b) most routed calls
    // still land on it. The new renderer piggy-backs on the same device.
    if (!legacy_.init(window)) {
        SDL_Log("HybridRenderer: legacy renderer init failed");
        return false;
    }
    legacyInitialised_ = true;

    // If the new renderer implements drawFrame, bring it up too -- sharing
    // the legacy renderer's device so both can issue commands.
    if (next_.supports(RendererFeature::DrawFrame) || next_.supports(RendererFeature::Init)) {
        if (!next_.init(window, legacy_.getDevice())) {
            SDL_Log("HybridRenderer: new renderer init failed -- continuing with legacy only");
        } else {
            nextInitialised_ = true;
        }
    }

    return true;
}

void HybridRenderer::drawFrame(glm::vec3 eye, float yaw, float pitch, float roll)
{
    if (nextInitialised_ && next_.supports(RendererFeature::DrawFrame))
        next_.drawFrame(eye, yaw, pitch, roll);
    else
        legacy_.drawFrame(eye, yaw, pitch, roll);
}

void HybridRenderer::quit()
{
    // Tear down new first so its buffers release before the shared device dies.
    if (nextInitialised_) {
        next_.quit();
        nextInitialised_ = false;
    }
    if (legacyInitialised_) {
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
