#pragma once

#include <SDL3/SDL_gpu.h>

namespace Boilerplate
{
/// @brief Load a compiled shader from disk and create an SDL GPU shader object.
///
/// Path construction uses std::filesystem::path so separators are always
/// native (no mixed `\` / `/` on Windows).
SDL_GPUShader* loadShader(SDL_GPUDevice* dev,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount);

} // namespace Boilerplate