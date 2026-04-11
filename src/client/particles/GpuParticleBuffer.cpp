/// @file GpuParticleBuffer.cpp
/// @brief Implementation of GPU/transfer buffer management for particle data.

#include "GpuParticleBuffer.hpp"

#include <cstring>

void GpuParticleBuffer::init(SDL_GPUDevice* dev, uint32_t maxBytes, Mode mode)
{
    device_ = dev;
    capacity_ = maxBytes;

    const SDL_GPUBufferUsageFlags usage =
        (mode == Mode::Storage) ? SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ : SDL_GPU_BUFFERUSAGE_VERTEX;

    SDL_GPUBufferCreateInfo bi{};
    bi.usage = usage;
    bi.size = maxBytes;
    gpuBuf_ = SDL_CreateGPUBuffer(dev, &bi);
    if (!gpuBuf_)
        SDL_Log("GpuParticleBuffer: SDL_CreateGPUBuffer failed: %s", SDL_GetError());

    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = maxBytes;
    transfer_ = SDL_CreateGPUTransferBuffer(dev, &ti);
    if (!transfer_)
        SDL_Log("GpuParticleBuffer: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
}

void GpuParticleBuffer::quit()
{
    if (device_) {
        if (gpuBuf_)
            SDL_ReleaseGPUBuffer(device_, gpuBuf_);
        if (transfer_)
            SDL_ReleaseGPUTransferBuffer(device_, transfer_);
    }
    gpuBuf_ = nullptr;
    transfer_ = nullptr;
    device_ = nullptr;
    capacity_ = 0;
    liveCount_ = 0;
}

void GpuParticleBuffer::upload(SDL_GPUCommandBuffer* cmd, const void* data, uint32_t count, uint32_t stride)
{
    liveCount_ = count;
    if (count == 0 || !gpuBuf_ || !transfer_)
        return;

    const uint32_t bytes = count * stride;
    if (bytes > capacity_) {
        SDL_Log("GpuParticleBuffer::upload: %u bytes exceeds capacity %u", bytes, capacity_);
        return;
    }

    // cycle=true: SDL_GPU allocates a fresh staging region each frame so the GPU
    // can still be reading from the previous frame's copy without a stall.
    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer_, /*cycle=*/true);
    if (!mapped) {
        SDL_Log("GpuParticleBuffer::upload: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        return;
    }
    std::memcpy(mapped, data, bytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer_);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transfer_;
    src.offset = 0;

    SDL_GPUBufferRegion dst{};
    dst.buffer = gpuBuf_;
    dst.offset = 0;
    dst.size = bytes;

    SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/false);
    SDL_EndGPUCopyPass(cp);
}

void GpuParticleBuffer::bindAsVertexStorage(SDL_GPURenderPass* pass, uint32_t slot) const
{
    if (gpuBuf_)
        SDL_BindGPUVertexStorageBuffers(pass, slot, &gpuBuf_, 1);
}

void GpuParticleBuffer::bindAsVertex(SDL_GPURenderPass* pass) const
{
    if (gpuBuf_) {
        SDL_GPUBufferBinding b{};
        b.buffer = gpuBuf_;
        b.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &b, 1);
    }
}
