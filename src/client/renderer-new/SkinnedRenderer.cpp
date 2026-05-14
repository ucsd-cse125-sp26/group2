/// @file SkinnedRenderer.cpp
/// @brief Implementation of the skinned-character subsystem.
///
/// Owns its own GPU resources (per-mesh VB/IB/boneVB, palette + instance
/// SSBOs, transfer buffers) and per-frame CPU staging.  Graphics team
/// only needs to touch this file (and add the matching shaders + pipeline)
/// to bring skinned characters to screen.
///
/// Grep `TODO(graphics)` to find every hook still awaiting work.

#include "SkinnedRenderer.hpp"

#include "Boilerplate.hpp"

#include <algorithm>
#include <cstring>

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void SkinnedRenderer::init(SDL_GPUDevice* device)
{
    device_ = device;
    // No GPU allocations here — buffers and pipeline are created on demand
    // (setRig allocates the rig mesh buffers; setFrame triggers SSBO growth).
    //
    // TODO(graphics): if you want the skinned pipeline created up-front, do
    // it here.  Otherwise leave it null and create it the first time
    // `draw()` would actually issue draws.
}

void SkinnedRenderer::shutdown()
{
    if (!device_)
        return;

    for (auto& sm : skinnedMeshes_) {
        if (sm.vb)
            SDL_ReleaseGPUBuffer(device_, sm.vb);
        if (sm.boneVb)
            SDL_ReleaseGPUBuffer(device_, sm.boneVb);
        if (sm.ib)
            SDL_ReleaseGPUBuffer(device_, sm.ib);
    }
    skinnedMeshes_.clear();

    if (palettesSsbo_)
        SDL_ReleaseGPUBuffer(device_, palettesSsbo_);
    if (instancesSsbo_)
        SDL_ReleaseGPUBuffer(device_, instancesSsbo_);
    if (paletteXfer_)
        SDL_ReleaseGPUTransferBuffer(device_, paletteXfer_);
    if (instanceXfer_)
        SDL_ReleaseGPUTransferBuffer(device_, instanceXfer_);
    if (pipeline_)
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);

    palettesSsbo_ = nullptr;
    instancesSsbo_ = nullptr;
    paletteXfer_ = nullptr;
    instanceXfer_ = nullptr;
    pipeline_ = nullptr;
    palettesCapacityBytes_ = 0;
    instancesCapacityBytes_ = 0;
    paletteXferCapacityBytes_ = 0;
    instanceXferCapacityBytes_ = 0;

    rigInstalled_ = false;
    numJoints_ = 0;
    frameDirty_ = false;
    framePalette_.clear();
    frameInstances_.clear();

    device_ = nullptr;
}

// ─── Rig install ─────────────────────────────────────────────────────────────

bool SkinnedRenderer::setRig(const std::vector<RigMeshSource>& meshes, int numJoints)
{
    if (!device_) {
        SDL_Log("SkinnedRenderer::setRig: not initialised (device==null)");
        return false;
    }
    if (rigInstalled_) {
        SDL_Log("SkinnedRenderer::setRig: rig already installed; ignoring repeat call");
        return false;
    }
    if (meshes.empty() || numJoints <= 0) {
        SDL_Log("SkinnedRenderer::setRig: empty meshes or numJoints<=0 (%zu, %d)", meshes.size(), numJoints);
        return false;
    }
    for (size_t i = 0; i < meshes.size(); ++i) {
        const auto& m = meshes[i];
        if (m.boneInfluences.size() != m.bindPoseVertices.size()) {
            SDL_Log("SkinnedRenderer::setRig: mesh %zu has %zu verts but %zu bone-influence entries",
                    i,
                    m.bindPoseVertices.size(),
                    m.boneInfluences.size());
            return false;
        }
        if (m.indices.empty()) {
            SDL_Log("SkinnedRenderer::setRig: mesh %zu has zero indices", i);
            return false;
        }
    }

    numJoints_ = numJoints;
    skinnedMeshes_.clear();
    skinnedMeshes_.reserve(meshes.size());

    // Single transfer buffer sized to the largest pending upload, reused
    // across all per-mesh uploads.
    Uint32 maxUploadBytes = 0;
    for (const auto& m : meshes) {
        const Uint32 vBytes = static_cast<Uint32>(m.bindPoseVertices.size() * sizeof(ModelVertex));
        const Uint32 bBytes = static_cast<Uint32>(m.boneInfluences.size() * sizeof(BoneInfluence));
        const Uint32 iBytes = static_cast<Uint32>(m.indices.size() * sizeof(uint32_t));
        maxUploadBytes = std::max({maxUploadBytes, vBytes, bBytes, iBytes});
    }
    SDL_GPUTransferBuffer* xfer = Boilerplate::createUploadBuffer(device_, maxUploadBytes);
    if (!xfer) {
        SDL_Log("SkinnedRenderer::setRig: createUploadBuffer(%u) failed: %s", maxUploadBytes, SDL_GetError());
        return false;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    auto uploadToBuffer = [&](SDL_GPUBuffer* dst, const void* src, Uint32 bytes) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, xfer, /*cycle=*/true);
        if (!mapped)
            return;
        SDL_memcpy(mapped, src, bytes);
        SDL_UnmapGPUTransferBuffer(device_, xfer);
        SDL_GPUTransferBufferLocation s{};
        s.transfer_buffer = xfer;
        s.offset = 0;
        SDL_GPUBufferRegion d{};
        d.buffer = dst;
        d.offset = 0;
        d.size = bytes;
        SDL_UploadToGPUBuffer(cp, &s, &d, /*cycle=*/false);
    };

    for (const auto& m : meshes) {
        SkinnedMesh sm{};
        sm.vertexCount = static_cast<Uint32>(m.bindPoseVertices.size());
        sm.indexCount = static_cast<Uint32>(m.indices.size());

        const Uint32 vBytes = sm.vertexCount * sizeof(ModelVertex);
        const Uint32 bBytes = sm.vertexCount * sizeof(BoneInfluence);
        const Uint32 iBytes = sm.indexCount * sizeof(uint32_t);

        sm.vb = Boilerplate::createBuffer(device_, vBytes, SDL_GPU_BUFFERUSAGE_VERTEX);
        sm.boneVb = Boilerplate::createBuffer(device_, bBytes, SDL_GPU_BUFFERUSAGE_VERTEX);
        sm.ib = Boilerplate::createBuffer(device_, iBytes, SDL_GPU_BUFFERUSAGE_INDEX);

        if (sm.vb && vBytes > 0)
            uploadToBuffer(sm.vb, m.bindPoseVertices.data(), vBytes);
        if (sm.boneVb && bBytes > 0)
            uploadToBuffer(sm.boneVb, m.boneInfluences.data(), bBytes);
        if (sm.ib && iBytes > 0)
            uploadToBuffer(sm.ib, m.indices.data(), iBytes);

        skinnedMeshes_.push_back(sm);
    }

    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device_);

    SDL_ReleaseGPUTransferBuffer(device_, xfer);

    rigInstalled_ = true;
    SDL_Log("SkinnedRenderer: rig installed — %zu mesh(es), %d joints", skinnedMeshes_.size(), numJoints);
    return true;
}

// ─── Per-frame: capture + upload ─────────────────────────────────────────────

void SkinnedRenderer::setFrame(const std::vector<glm::mat4>& palette, const std::vector<SkinnedInstance>& instances)
{
    framePalette_ = palette;
    frameInstances_ = instances;
    frameDirty_ = !instances.empty();
}

bool SkinnedRenderer::ensureSsbos(Uint32 paletteBytes, Uint32 instanceBytes)
{
    auto growBuf = [&](SDL_GPUBuffer*& buf, Uint32& cap, Uint32 want, SDL_GPUBufferUsageFlags use) {
        if (want == 0)
            return true;
        if (want > cap) {
            if (buf)
                SDL_ReleaseGPUBuffer(device_, buf);
            SDL_GPUBufferCreateInfo bInfo{};
            bInfo.usage = use;
            bInfo.size = want;
            buf = SDL_CreateGPUBuffer(device_, &bInfo);
            cap = buf ? want : 0;
            return buf != nullptr;
        }
        return true;
    };
    auto growXfer = [&](SDL_GPUTransferBuffer*& xfer, Uint32& cap, Uint32 want) {
        if (want == 0)
            return true;
        if (want > cap) {
            if (xfer)
                SDL_ReleaseGPUTransferBuffer(device_, xfer);
            SDL_GPUTransferBufferCreateInfo tbInfo{};
            tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbInfo.size = want;
            xfer = SDL_CreateGPUTransferBuffer(device_, &tbInfo);
            cap = xfer ? want : 0;
            return xfer != nullptr;
        }
        return true;
    };
    if (!growBuf(palettesSsbo_, palettesCapacityBytes_, paletteBytes, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ))
        return false;
    if (!growBuf(instancesSsbo_, instancesCapacityBytes_, instanceBytes, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ))
        return false;
    if (!growXfer(paletteXfer_, paletteXferCapacityBytes_, paletteBytes))
        return false;
    if (!growXfer(instanceXfer_, instanceXferCapacityBytes_, instanceBytes))
        return false;
    return true;
}

void SkinnedRenderer::uploadFrame(SDL_GPUCommandBuffer* /*cmd*/, SDL_GPUCopyPass* copyPass)
{
    if (!frameDirty_ || frameInstances_.empty() || !device_)
        return;

    const Uint32 paletteBytes = static_cast<Uint32>(framePalette_.size() * sizeof(glm::mat4));
    const Uint32 instanceBytes = static_cast<Uint32>(frameInstances_.size() * sizeof(SkinnedInstance));
    if (!ensureSsbos(paletteBytes, instanceBytes))
        return;

    auto upload = [&](SDL_GPUTransferBuffer* xfer, SDL_GPUBuffer* dst, const void* src, Uint32 bytes) {
        if (bytes == 0 || !xfer || !dst)
            return;
        void* mapped = SDL_MapGPUTransferBuffer(device_, xfer, /*cycle=*/true);
        if (!mapped)
            return;
        SDL_memcpy(mapped, src, bytes);
        SDL_UnmapGPUTransferBuffer(device_, xfer);
        SDL_GPUTransferBufferLocation s{};
        s.transfer_buffer = xfer;
        s.offset = 0;
        SDL_GPUBufferRegion d{};
        d.buffer = dst;
        d.offset = 0;
        d.size = bytes;
        SDL_UploadToGPUBuffer(copyPass, &s, &d, /*cycle=*/false);
    };
    upload(paletteXfer_, palettesSsbo_, framePalette_.data(), paletteBytes);
    upload(instanceXfer_, instancesSsbo_, frameInstances_.data(), instanceBytes);
}

// ─── Per-frame: draw ─────────────────────────────────────────────────────────

void SkinnedRenderer::draw(SDL_GPURenderPass* /*renderPass*/, SDL_GPUCommandBuffer* /*cmd*/)
{
    // TODO(graphics): instanced GPU skinning draw call.  See `setFrame`
    // doc-block for the data layout and shader pseudocode.  Sketch:
    //
    //   if (!rigInstalled_ || !pipeline_ || frameInstances_.empty()
    //       || !palettesSsbo_ || !instancesSsbo_)
    //       return;
    //
    //   SDL_BindGPUGraphicsPipeline(renderPass, pipeline_);
    //
    //   // The geometry pass already pushed view+projection at vertex UBO
    //   // slot 0 in NewRenderer::drawGeometryPass — no need to push again.
    //
    //   SDL_GPUBuffer* ssbos[2] = {palettesSsbo_, instancesSsbo_};
    //   SDL_BindGPUVertexStorageBuffers(renderPass, 0, ssbos, 2);
    //
    //   const Uint32 numInstances = static_cast<Uint32>(frameInstances_.size());
    //   for (const auto& sm : skinnedMeshes_) {
    //       const SDL_GPUBufferBinding vbs[2] = {
    //           {.buffer = sm.vb,     .offset = 0},
    //           {.buffer = sm.boneVb, .offset = 0},
    //       };
    //       SDL_BindGPUVertexBuffers(renderPass, 0, vbs, 2);
    //       const SDL_GPUBufferBinding ib = {.buffer = sm.ib, .offset = 0};
    //       SDL_BindGPUIndexBuffer(renderPass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    //       SDL_DrawGPUIndexedPrimitives(renderPass, sm.indexCount, numInstances, 0, 0, 0);
    //   }
    //
    // Pipeline (`pipeline_`) needs:
    //   - vertex buffer 0: ModelVertex
    //       location 0 = position  vec3 (offset  0)
    //       location 1 = normal    vec3 (offset 12)
    //       location 2 = texCoord  vec2 (offset 24)
    //       location 3 = tangent   vec4 (offset 32)
    //   - vertex buffer 1: BoneInfluence
    //       location 4 = boneIndices ivec4 (offset  0)
    //       location 5 = boneWeights vec4  (offset 16)
    //   - 2 vertex storage buffers (BonePalette + Instances)
    //   - 1 vertex UBO at slot 0 (view-projection — pushed by NewRenderer)
    //   - depth test on, cull mode NONE (rig may be double-sided)
    //   - colour target = same format as the geometry pass currently uses
    //     (today: swapchain format; later: NewRenderer::getHdrFormat()).
    //
    // Shader pseudocode (`shaders-new/geometry_skinned.vert`):
    //   InstanceData inst = instances[gl_InstanceIndex];
    //   mat4 skin = palette[inst.paletteBase + uint(inBoneIndices.x)] * inBoneWeights.x
    //             + palette[inst.paletteBase + uint(inBoneIndices.y)] * inBoneWeights.y
    //             + palette[inst.paletteBase + uint(inBoneIndices.z)] * inBoneWeights.z
    //             + palette[inst.paletteBase + uint(inBoneIndices.w)] * inBoneWeights.w;
    //   vec4 worldPos = inst.worldTransform * skin * vec4(inPosition, 1.0);
    //   gl_Position   = viewProjection * worldPos;
}
