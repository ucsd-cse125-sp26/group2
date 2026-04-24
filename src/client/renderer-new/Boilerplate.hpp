#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <backends/imgui_impl_sdlgpu3.h>
#include <cstddef>
#include <vector>

namespace Boilerplate
{
struct ShaderInfo
{
    const char* path = nullptr;
    SDL_GPUShaderStage stage = SDL_GPU_SHADERSTAGE_VERTEX;
    Uint32 samplerCount = 0;
    Uint32 uniformBufferCount = 0;
    Uint32 storageBufferCount = 0;
    Uint32 storageTextureCount = 0;
};

struct VertexInputLayout
{
    Uint32 vertexPitch = 0;
    std::vector<SDL_GPUVertexAttribute> attributes;
};

struct BufferUpload
{
    SDL_GPUBuffer* buffer = nullptr;
    const void* data = nullptr;
    size_t size = 0;
};

ImGui_ImplSDLGPU3_InitInfo createImGuiInfo(SDL_GPUDevice* device, SDL_Window* window);

SDL_GPUVertexAttribute
makeAttribute(Uint32 location, SDL_GPUVertexElementFormat format, Uint32 offset, Uint32 bufferSlot = 0);

SDL_GPUColorTargetInfo makeColorTarget(SDL_GPUTexture* texture, SDL_FColor clearColor);

SDL_GPUDepthStencilTargetInfo makeDepthTarget(SDL_GPUTexture* texture);

SDL_GPUTextureSamplerBinding makeTextureSamplerBinding(SDL_GPUTexture* texture, SDL_GPUSampler* sampler);

SDL_GPUShaderFormat selectShaderFormat(SDL_GPUDevice* device);

SDL_GPUShader* loadShader(SDL_GPUDevice* device,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount);

SDL_GPUShader* loadShader(SDL_GPUDevice* device, const ShaderInfo& shaderInfo, SDL_GPUShaderFormat format);

SDL_GPUGraphicsPipeline* createGraphicsPipeline(SDL_GPUDevice* device,
                                                SDL_Window* window,
                                                SDL_GPUShaderFormat shaderFormat,
                                                const ShaderInfo& vertexShaderInfo,
                                                const ShaderInfo& fragmentShaderInfo,
                                                const VertexInputLayout& vertexInputLayout,
                                                bool enableDepth = true);

SDL_GPUBuffer* createBuffer(SDL_GPUDevice* device, size_t bufferSize, SDL_GPUBufferUsageFlags usage);

SDL_GPUTransferBuffer* createTransferBuffer(SDL_GPUDevice* device, size_t transferBufferSize, bool upload);

SDL_GPUTransferBuffer* createUploadBuffer(SDL_GPUDevice* device, size_t transferBufferSize);

void uploadBuffers(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd, const std::vector<BufferUpload>& uploads);

SDL_GPUTexture* createTextureRGBA8(SDL_GPUDevice* device, Uint32 width, Uint32 height, const void* data);

SDL_GPUTexture* loadTexture(SDL_GPUDevice* device, const char* path);

SDL_GPUTexture* createDepthTexture(SDL_GPUDevice* device, Uint32 width, Uint32 height);

SDL_GPUSampler* createLinearRepeatSampler(SDL_GPUDevice* device);

} // namespace Boilerplate
