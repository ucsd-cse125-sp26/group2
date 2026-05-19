/// @file VolumeFireEffect.hpp
/// @brief Runtime playback for generated fire/explosion flipbook previews.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "particles/GpuParticleBuffer.hpp"
#include "renderer-new/Camera.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class VolumeFireEffect
{
public:
    bool init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt);
    void quit();

    void update(float dt, Registry& registry);
    void uploadToGpu(SDL_GPUCommandBuffer* cmd);
    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const NewCamera& camera);
    void spawnBloodHit(glm::vec3 pos, glm::vec3 normal);

    [[nodiscard]] uint32_t instanceCount() const
    {
        return static_cast<uint32_t>(flipbookPreviews_.size() + transientFlipbooks_.size() + instances_.size());
    }
    [[nodiscard]] uint32_t currentFrame() const { return currentFrame_; }
    [[nodiscard]] bool ready() const { return ready_; }

private:
    struct VolumeFireInstanceGPU
    {
        glm::vec3 boxMin;
        float opacity;
        glm::vec3 boxMax;
        float _pad0;
    };
    static_assert(sizeof(VolumeFireInstanceGPU) == 32);

    struct FlipbookPreview
    {
        SDL_GPUTexture* texture = nullptr;
        uint32_t tileWidth = 0;
        uint32_t tileHeight = 0;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        uint32_t columns = 0;
        uint32_t rows = 0;
        uint32_t frameCount = 0;
        float fps = 24.0f;
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
        float opacity = 1.0f;
        glm::vec3 bottomCenter{0.0f};
        std::string name;
    };

    struct TransientFlipbook
    {
        uint32_t previewIndex = UINT32_MAX;
        glm::vec3 center{0.0f};
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
        float opacity = 1.0f;
        float age = 0.0f;
        float lifetime = 0.0f;
    };

    bool loadCache(const std::string& path);
    bool loadFlipbook(const std::string& path);
    bool loadFlipbookPreview(const std::string& path,
                             const glm::vec3& bottomCenter,
                             float worldScale,
                             float opacity,
                             const std::string& name);
    void renderFlipbook(SDL_GPURenderPass* pass,
                        SDL_GPUCommandBuffer* cmd,
                        const NewCamera& camera,
                        const FlipbookPreview& preview,
                        const glm::vec3& center,
                        float halfWidth,
                        float halfHeight,
                        float opacity,
                        float age,
                        bool loop) const;
    bool createSampler();
    bool createGpuResources();
    bool buildPipeline(SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt);
    bool buildFlipbookPipeline(SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt);
    bool createFlipbookTexture();
    void uploadFrameToTexture(SDL_GPUCommandBuffer* cmd, uint32_t slot, uint32_t frameIndex);
    uint32_t findTextureSlot(uint32_t frameIndex) const;

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFmt_ = SDL_GPU_SHADERFORMAT_INVALID;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* flipbookPipeline_ = nullptr;
    SDL_GPUTexture* frameTextures_[2]{};
    SDL_GPUTexture* flipbookTexture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    SDL_GPUTransferBuffer* frameTransfer_ = nullptr;
    GpuParticleBuffer instanceBuf_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t depth_ = 0;
    uint32_t frameCount_ = 0;
    float fps_ = 24.0f;
    float animTime_ = 0.0f;
    uint32_t currentFrame_ = 0;
    uint32_t nextFrame_ = 0;
    float frameBlend_ = 0.0f;
    uint64_t frameBytes_ = 0;
    uint32_t textureFrame_[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t flipbookTileWidth_ = 0;
    uint32_t flipbookTileHeight_ = 0;
    uint32_t flipbookAtlasWidth_ = 0;
    uint32_t flipbookAtlasHeight_ = 0;
    uint32_t flipbookColumns_ = 0;
    uint32_t flipbookRows_ = 0;
    uint32_t flipbookFrameCount_ = 0;
    uint64_t flipbookPayloadBytes_ = 0;
    std::vector<uint16_t> frames_;
    std::vector<uint16_t> flipbookPixels_;
    std::vector<VolumeFireInstanceGPU> instances_;
    std::vector<FlipbookPreview> flipbookPreviews_;
    std::vector<TransientFlipbook> transientFlipbooks_;
    uint32_t bloodFlipbookIndex_ = UINT32_MAX;
    glm::vec3 volumeBottomCenter_{660.0f, 50.0f, 500.0f};
    glm::vec3 volumeWorldSize_{120.0f};
    float volumeOpacity_ = 0.92f;
    bool volumeReady_ = false;
    bool ready_ = false;
    bool flipbookReady_ = false;
};
