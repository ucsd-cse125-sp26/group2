/// @file Boilerplate.hpp
/// @brief SDL3 GPU helper utilities: shader loading, buffer/texture creation, pipeline setup.

#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <backends/imgui_impl_sdlgpu3.h>
#include <cstddef>
#include <vector>

namespace Boilerplate
{
/// @brief Descriptor for a single shader stage: file path, stage, and resource counts.
struct ShaderInfo
{
    const char* path = nullptr;
    SDL_GPUShaderStage stage = SDL_GPU_SHADERSTAGE_VERTEX;
    Uint32 samplerCount = 0;
    Uint32 uniformBufferCount = 0;
    Uint32 storageBufferCount = 0;
    Uint32 storageTextureCount = 0;
};

/// @brief Vertex buffer layout: stride (pitch) and per-attribute descriptions.
struct VertexInputLayout
{
    // Uint32 vertexPitch = 0;
    std::vector<SDL_GPUVertexBufferDescription> bufferDescriptions;
    std::vector<SDL_GPUVertexAttribute> attributes;
};

/// @brief Describes a pending CPU-to-GPU buffer upload: target buffer, source data, and byte size.
struct BufferUpload
{
    SDL_GPUBuffer* buffer = nullptr;
    const void* data = nullptr;
    size_t size = 0;
};

/// @brief Create an ImGui initialization info struct for SDL3 GPU rendering.
/// @param device The GPU device.
/// @param window The SDL window.
/// @return Populated ImGui_ImplSDLGPU3_InitInfo.
ImGui_ImplSDLGPU3_InitInfo createImGuiInfo(SDL_GPUDevice* device, SDL_Window* window);

/// @brief Build an SDL_GPUVertexAttribute descriptor.
/// @param location Shader attribute location index.
/// @param format Element format (e.g. FLOAT3).
/// @param offset Byte offset within the vertex.
/// @param bufferSlot Vertex buffer slot (default 0).
/// @return The populated vertex attribute.
SDL_GPUVertexAttribute
makeAttribute(Uint32 location, SDL_GPUVertexElementFormat format, Uint32 offset, Uint32 bufferSlot = 0);

/// @brief Create a color render-target info with a clear color.
/// @param texture The target texture.
/// @param clearColor The RGBA clear color.
/// @return Populated SDL_GPUColorTargetInfo.
SDL_GPUColorTargetInfo makeColorTargetClear(SDL_GPUTexture* texture, SDL_FColor clearColor);

/// @brief Create a color render-target info with a clear color.
/// @param texture The target texture.
/// @return Populated SDL_GPUColorTargetInfo.
SDL_GPUColorTargetInfo makeColorTargetLoad(SDL_GPUTexture* texture);

/// @brief Create a depth/stencil render-target info that clears to depth 1.0.
/// @param texture The depth texture.
/// @return Populated SDL_GPUDepthStencilTargetInfo.
SDL_GPUDepthStencilTargetInfo makeDepthTarget(SDL_GPUTexture* texture);

/// @brief Create a texture-sampler binding pair for fragment shader use.
/// @param texture The GPU texture.
/// @param sampler The GPU sampler.
/// @return Populated SDL_GPUTextureSamplerBinding.
SDL_GPUTextureSamplerBinding makeTextureSamplerBinding(SDL_GPUTexture* texture, SDL_GPUSampler* sampler);

/// @brief Pick the best available shader format for the given device.
/// @param device The GPU device.
/// @return The selected SDL_GPUShaderFormat, or INVALID if none found.
SDL_GPUShaderFormat selectShaderFormat(SDL_GPUDevice* device);

/// @brief Load and compile a shader from disk (explicit parameters).
/// @param device The GPU device.
/// @param path Base path to the shader file (extension added automatically).
/// @param format The shader binary format.
/// @param stage Vertex or fragment stage.
/// @param samplerCount Number of samplers used by the shader.
/// @param uniformBufferCount Number of uniform buffers used.
/// @param storageBufferCount Number of storage buffers used.
/// @param storageTextureCount Number of storage textures used.
/// @return The compiled GPU shader, or nullptr on failure.
SDL_GPUShader* loadShader(SDL_GPUDevice* device,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount);

/// @brief Load and compile a shader from a ShaderInfo descriptor.
/// @param device The GPU device.
/// @param shaderInfo Shader descriptor with path, stage, and resource counts.
/// @param format The shader binary format.
/// @return The compiled GPU shader, or nullptr on failure.
SDL_GPUShader* loadShader(SDL_GPUDevice* device, const ShaderInfo& shaderInfo, SDL_GPUShaderFormat format);

/// @brief Create a full graphics pipeline from vertex/fragment shaders and vertex layout.
/// @param device The GPU device.
/// @param colorFormat
/// @param shaderFormat The shader binary format.
/// @param vertexShaderInfo Vertex shader descriptor.
/// @param fragmentShaderInfo Fragment shader descriptor.
/// @param vertexInputLayout Vertex buffer layout description.
/// @param enableDepth Enable depth testing and writing (default true).
/// @param overBlending
/// @return The created pipeline, or nullptr on failure.
SDL_GPUGraphicsPipeline* createGraphicsPipeline(SDL_GPUDevice* device,
                                                SDL_GPUTextureFormat& colorFormat,
                                                SDL_GPUShaderFormat shaderFormat,
                                                const ShaderInfo& vertexShaderInfo,
                                                const ShaderInfo& fragmentShaderInfo,
                                                const VertexInputLayout& vertexInputLayout,
                                                bool enableDepth = true,
                                                bool overBlending = false);

/// @brief Allocate a GPU buffer of the given size and usage.
/// @param device The GPU device.
/// @param bufferSize Size in bytes.
/// @param usage Buffer usage flags (vertex, index, etc.).
/// @return The created GPU buffer.
SDL_GPUBuffer* createBuffer(SDL_GPUDevice* device, size_t bufferSize, SDL_GPUBufferUsageFlags usage);

/// @brief Allocate a GPU transfer buffer for upload or download.
/// @param device The GPU device.
/// @param transferBufferSize Size in bytes.
/// @param upload True for upload, false for download.
/// @return The created transfer buffer.
SDL_GPUTransferBuffer* createTransferBuffer(SDL_GPUDevice* device, size_t transferBufferSize, bool upload);

/// @brief Allocate a GPU transfer buffer for uploading data.
/// @param device The GPU device.
/// @param transferBufferSize Size in bytes.
/// @return The created upload transfer buffer.
SDL_GPUTransferBuffer* createUploadBuffer(SDL_GPUDevice* device, size_t transferBufferSize);

/// @brief Batch-upload multiple CPU buffers to their corresponding GPU buffers.
/// @param device The GPU device.
/// @param cmd The command buffer to record copy commands into.
/// @param uploads Vector of BufferUpload descriptors.
void uploadBuffers(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd, const std::vector<BufferUpload>& uploads);

/// @brief Create a 2D RGBA8 texture and upload pixel data to it.
/// @param device The GPU device.
/// @param width Texture width in pixels.
/// @param height Texture height in pixels.
/// @param data Pointer to RGBA8 pixel data.
/// @return The created GPU texture, or nullptr on failure.
SDL_GPUTexture* createTextureRGBA8(SDL_GPUDevice* device, Uint32 width, Uint32 height, const void* data);

/// @brief Load an image file from disk and create a GPU texture from it.
/// @param device The GPU device.
/// @param path Path to the image file.
/// @return The created GPU texture, or nullptr on failure.
SDL_GPUTexture* loadTexture(SDL_GPUDevice* device, const char* path);

/// @brief Create a D32_FLOAT depth texture of the given dimensions.
/// @param device The GPU device.
/// @param width Texture width in pixels.
/// @param height Texture height in pixels.
/// @return The created depth texture, or nullptr on failure.
SDL_GPUTexture* createDepthTexture(SDL_GPUDevice* device, Uint32 width, Uint32 height);

/// @brief Create a sampleable 2D color render target of the given dimensions.
SDL_GPUTexture*
createSampledColorTarget(SDL_GPUDevice* device, Uint32 width, Uint32 height, SDL_GPUTextureFormat format);

/// @brief Create a linear-filtering, repeat-addressing sampler.
/// @param device The GPU device.
/// @return The created GPU sampler.
SDL_GPUSampler* createLinearRepeatSampler(SDL_GPUDevice* device);

/// @brief Create a linear-filtering, clamp-to-edge sampler.
/// @param device The GPU device.
/// @return The created GPU sampler.
SDL_GPUSampler* createLinearClampSampler(SDL_GPUDevice* device);

} // namespace Boilerplate
