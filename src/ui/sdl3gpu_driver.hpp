#pragma once
#include <SDL3/SDL.h>

#include <Ultralight/platform/GPUDriver.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

class SDL3GPUDriver : public ultralight::GPUDriver
{
public:
    explicit SDL3GPUDriver(SDL_GPUDevice* device);
    ~SDL3GPUDriver() override;

    bool buildPipelines(const char* basePath, SDL_Window* window);
    void flushCommands(SDL_GPUCommandBuffer* cmdBuf);
    SDL_GPUTexture* getTexture(uint32_t textureId) const;
    SDL_GPUSampler* getSampler(uint32_t textureId) const;
    SDL_GPUGraphicsPipeline* getCompositePipeline() const { return compositePipeline; }

    // ultralight::GPUDriver interface
    void BeginSynchronize() override;
    void EndSynchronize() override;
    uint32_t NextTextureId() override;
    void CreateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm) override;
    void UpdateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm) override;
    void DestroyTexture(uint32_t id) override;
    uint32_t NextRenderBufferId() override;
    void CreateRenderBuffer(uint32_t id, const ultralight::RenderBuffer& rb) override;
    void DestroyRenderBuffer(uint32_t id) override;
    uint32_t NextGeometryId() override;
    void CreateGeometry(uint32_t id, const ultralight::VertexBuffer& vb, const ultralight::IndexBuffer& ib) override;
    void UpdateGeometry(uint32_t id, const ultralight::VertexBuffer& vb, const ultralight::IndexBuffer& ib) override;
    void DestroyGeometry(uint32_t id) override;
    void UpdateCommandList(const ultralight::CommandList& list) override;

private:
    struct GpuTexture
    {
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pendingData;
        SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        bool dirty = false;
    };

    struct GpuGeometry
    {
        SDL_GPUBuffer* vertexBuf = nullptr;
        SDL_GPUBuffer* indexBuf = nullptr;
        uint32_t indexCount = 0;
        ultralight::VertexBufferFormat vfmt = ultralight::VertexBufferFormat::_2f_4ub_2f_2f_28f;
    };

    struct GpuRenderBuffer
    {
        uint32_t textureId = 0;
    };

    struct PendingCommand
    {
        ultralight::Command cmd;
    };

    struct alignas(16) VertexUniforms
    {
        float transform[16];
    };

    struct alignas(16) FragUniforms
    {
        float state[4];
        float transform[16];
        float scalar4[8];
        float vector[32];
        uint32_t clipSize;
        uint32_t pad[3];
        float clip[8][16];
    };

    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* fillPipeline = nullptr;
    SDL_GPUGraphicsPipeline* fillPathPipeline = nullptr;
    SDL_GPUGraphicsPipeline* compositePipeline = nullptr;
    SDL_GPUTexture* dummyTexture = nullptr;
    SDL_GPUSampler* dummySampler = nullptr;

    std::unordered_map<uint32_t, GpuTexture> textures;
    std::unordered_map<uint32_t, GpuGeometry> geometry;
    std::unordered_map<uint32_t, GpuRenderBuffer> renderBuffers;

    std::vector<PendingCommand> pendingCommands;
    uint32_t nextTexId = 1;
    uint32_t nextRbId = 1;
    uint32_t nextGeoId = 1;
    bool commandsEverFlushed = false;

    SDL_GPUShader*
    loadSPIRV(const char* path, SDL_GPUShaderStage stage, uint32_t numSamplers, uint32_t numUniformBuffers);
    SDL_GPUGraphicsPipeline* buildPipelineInternal(SDL_GPUShader* vert,
                                                   SDL_GPUShader* frag,
                                                   ultralight::VertexBufferFormat vfmt,
                                                   SDL_GPUTextureFormat targetFmt,
                                                   bool enableBlend);
    void uploadDirtyTextures(SDL_GPUCommandBuffer* cmdBuf);
    void executeCommand(SDL_GPUCommandBuffer* cmdBuf, const ultralight::Command& cmd);
    void fillVertexUniforms(VertexUniforms& out, const ultralight::GPUState& s) const;
    void fillFragUniforms(FragUniforms& out, const ultralight::GPUState& s) const;
};
