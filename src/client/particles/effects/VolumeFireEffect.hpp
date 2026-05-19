/// @file VolumeFireEffect.hpp
/// @brief Runtime playback and raymarched rendering for generated animated fire volumes.

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

    [[nodiscard]] uint32_t instanceCount() const { return static_cast<uint32_t>(instances_.size()); }
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

    bool loadCache(const std::string& path);
    bool createGpuResources();
    bool buildPipeline(SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt);
    void uploadFrameToTexture(SDL_GPUCommandBuffer* cmd, uint32_t slot, uint32_t frameIndex);
    uint32_t findTextureSlot(uint32_t frameIndex) const;

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFmt_ = SDL_GPU_SHADERFORMAT_INVALID;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUTexture* frameTextures_[2]{};
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
    uint32_t currentTextureSlot_ = 0;
    uint32_t nextTextureSlot_ = 1;
    std::vector<uint16_t> frames_;
    std::vector<VolumeFireInstanceGPU> instances_;
    bool ready_ = false;
};
