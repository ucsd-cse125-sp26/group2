/// @file GpuParticleBuffer.hpp
/// @brief GPU storage/vertex buffer paired with a CPU transfer buffer for particle uploads.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

/// @brief Manages a GPU storage buffer + CPU-side transfer buffer pair for particle data.
///
/// Two usage modes are supported:
///
///   Storage mode (default):
///     GPU buffer created with SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ.
///     Vertex shaders read particle data as a readonly storage buffer via
///     gl_InstanceIndex.  Bound with bindAsVertexStorage().
///
///   Vertex mode:
///     GPU buffer created with SDL_GPU_BUFFERUSAGE_VERTEX.
///     Used for pre-expanded flat vertex streams (ribbon trails, lightning arcs).
///     Bound with bindAsVertex().
///
/// In both cases upload() must be called inside a copy pass, BEFORE the render pass begins.
class GpuParticleBuffer
{
public:
    enum class Mode
    {
        Storage,
        Vertex
    };

    /// @brief Allocate GPU + transfer buffers of maxBytes each.
    void init(SDL_GPUDevice* dev, uint32_t maxBytes, Mode mode = Mode::Storage);

    /// @brief Release all GPU resources.
    void quit();

    /// @brief Upload count * stride bytes from data to the GPU buffer.
    ///
    /// Opens and closes its own SDL_GPUCopyPass internally.
    /// Must be called BEFORE SDL_BeginGPURenderPass.
    void upload(SDL_GPUCommandBuffer* cmd, const void* data, uint32_t count, uint32_t stride);

    /// @brief Bind as a vertex storage buffer at the given slot.
    /// @pre Mode::Storage, called inside a render pass.
    void bindAsVertexStorage(SDL_GPURenderPass* pass, uint32_t slot) const;

    /// @brief Bind as a vertex buffer at binding index 0.
    /// @pre Mode::Vertex, called inside a render pass.
    void bindAsVertex(SDL_GPURenderPass* pass) const;

    [[nodiscard]] uint32_t liveCount() const { return liveCount_; }
    [[nodiscard]] uint32_t capacityBytes() const { return capacity_; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUBuffer* gpuBuf_ = nullptr;
    SDL_GPUTransferBuffer* transfer_ = nullptr;
    uint32_t capacity_ = 0;  ///< Bytes allocated.
    uint32_t liveCount_ = 0; ///< Updated by upload().
};
