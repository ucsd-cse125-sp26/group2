#include <SDL3/SDL_gpu.h>

#include <filesystem>

namespace Boilerplate
{
SDL_GPUShader* loadShader(SDL_GPUDevice* dev,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount)
{
    const bool isMsl = format == SDL_GPU_SHADERFORMAT_MSL;
    const char* const base = SDL_GetBasePath();

    std::filesystem::path fullPath = base ? base : "";
    fullPath /= path;
    fullPath += isMsl ? ".msl" : ".spv";

    std::string fullPathStr = fullPath.string();

    size_t codeSize = 0;
    void* code = SDL_LoadFile(fullPathStr.c_str(), &codeSize);
    if (!code) {
        SDL_Log("loadShader: failed to load shader (%s): %s", fullPathStr.c_str(), SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{.code_size = static_cast<Uint32>(codeSize),
                                 .code = static_cast<const Uint8*>(code),
                                 .entrypoint = isMsl ? "main0" : "main",
                                 .format = format,
                                 .stage = stage,
                                 .num_samplers = samplerCount,
                                 .num_uniform_buffers = uniformBufferCount,
                                 .num_storage_buffers = storageBufferCount,
                                 .num_storage_textures = storageTextureCount};

    SDL_GPUShader* shader = SDL_CreateGPUShader(dev, &info);
    SDL_free(code);

    if (!shader) {
        SDL_Log("loadShader: SDL_CreateGPUShader(%s) failed: %s", fullPathStr.c_str(), SDL_GetError());
    }

    return shader;
}
} // namespace Boilerplate