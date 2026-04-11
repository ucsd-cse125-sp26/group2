/// @file ShaderUtils.hpp
/// @brief Shared shader-loading utilities used by Renderer and ParticleRenderer.

#pragma once

#include <SDL3/SDL.h>

/// @brief Load a compiled shader from disk and create an SDL GPU shader object.
/// @param dev                  The GPU device.
/// @param path                 Path to the compiled shader file (.spv or .msl).
/// @param format               Shader format (SPIR-V or MSL).
/// @param stage                Vertex or fragment stage.
/// @param samplerCount         Number of texture samplers declared in the shader.
/// @param uniformBufferCount   Number of uniform buffers declared in the shader.
/// @param storageBufferCount   Number of storage buffers declared in the shader.
/// @param storageTextureCount  Number of storage textures declared in the shader.
/// @return The created shader, or nullptr on failure (error logged via SDL_Log).
SDL_GPUShader* loadShader(SDL_GPUDevice* dev,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount);
