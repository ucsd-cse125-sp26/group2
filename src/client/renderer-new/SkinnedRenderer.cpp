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
#include <iostream>
#include <limits>

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void SkinnedRenderer::init(SDL_GPUDevice* device,
                           SDL_GPUTextureFormat& colorTarget,
                           const SDL_GPUShaderFormat& shaderFormat)
{
    device_ = device;
    colorFormat_ = colorTarget;
    shaderFormat_ = shaderFormat;
    // No GPU allocations here — buffers and pipeline are created on demand
    // (setRig allocates the rig mesh buffers; setFrame triggers SSBO growth).
    //
    // TODO(graphics): if you want the skinned pipeline created up-front, do
    // it here.  Otherwise leave it null and create it the first time
    // `draw()` would actually issue draws.
    createSkinningPipeline(colorTarget, shaderFormat);
    createSkinnedDepthPipeline(shaderFormat);
    createChamsPipeline();
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

    if (palettesSsboInfo_.ssbo_)
        SDL_ReleaseGPUBuffer(device_, palettesSsboInfo_.ssbo_);
    if (instancesSsboInfo_.ssbo_)
        SDL_ReleaseGPUBuffer(device_, instancesSsboInfo_.ssbo_);
    if (paletteXfer_)
        SDL_ReleaseGPUTransferBuffer(device_, paletteXfer_);
    if (instanceXfer_)
        SDL_ReleaseGPUTransferBuffer(device_, instanceXfer_);
    if (pipeline_)
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    if (depthPipeline_)
        SDL_ReleaseGPUGraphicsPipeline(device_, depthPipeline_);
    if (chamsPipeline_)
        SDL_ReleaseGPUGraphicsPipeline(device_, chamsPipeline_);

    palettesSsboInfo_.ssbo_ = nullptr;
    instancesSsboInfo_.ssbo_ = nullptr;
    paletteXfer_ = nullptr;
    instanceXfer_ = nullptr;
    pipeline_ = nullptr;
    depthPipeline_ = nullptr;
    chamsPipeline_ = nullptr;
    palettesSsboInfo_.capacityBytes_ = 0;
    instancesSsboInfo_.capacityBytes_ = 0;
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

    // Bind-pose bounding sphere (rig-local space) for per-instance frustum
    // culling in setFrame.  AABB over every mesh vertex → centre + farthest
    // corner, then padded so animations that fling limbs past the bind pose
    // (jumps, dashes, weapon poses) do not clip the character out early.
    {
        constexpr float kAnimationMargin = 1.5f;
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());
        bool anyVerts = false;
        for (const auto& m : meshes) {
            for (const auto& v : m.bindPoseVertices) {
                mn = glm::min(mn, v.position);
                mx = glm::max(mx, v.position);
                anyVerts = true;
            }
        }
        if (anyVerts) {
            rigBoundingCenter_ = 0.5f * (mn + mx);
            rigBoundingRadius_ = 0.5f * glm::length(mx - mn) * kAnimationMargin;
        } else {
            rigBoundingCenter_ = glm::vec3(0.0f);
            rigBoundingRadius_ = 0.0f;
        }
    }

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
        SDL_UploadToGPUBuffer(cp, &s, &d, /*cycle=*/true);
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

bool SkinnedRenderer::sphereInFrustum(const glm::vec3& center, float radius, const FrustumPlanes& planes)
{
    const glm::vec4 sides[NUM_FRUSTUM_PLANES] = {
        planes.left, planes.right, planes.bottom, planes.top, planes.near, planes.far};
    for (const glm::vec4& pl : sides) {
        const glm::vec3 n(pl);
        const float len = glm::length(n);
        if (len < 1e-8f)
            continue; // Degenerate plane (shouldn't happen) — don't let it cull everything.
        // Signed distance of the centre to the plane, normalised so `radius`
        // is comparable.  Inside is >= 0 (Gribb-Hartmann convention); the
        // sphere is fully outside this plane only when the centre is more than
        // `radius` behind it.
        const float dist = (glm::dot(n, center) + pl.w) / len;
        if (dist < -radius)
            return false;
    }
    return true;
}

void SkinnedRenderer::setFrame(const std::vector<glm::mat4>& palette,
                               const std::vector<SkinnedInstance>& instances,
                               const FrustumPlanes& frustum)
{
    framePalette_.clear();
    frameInstances_.clear();
    visibleInstanceCount_ = 0;
    chamsInstanceIndex_ = -1;

    // Record the index of the killcam-flagged instance (materialId == 1) so the
    // chams pass can draw just that one. Scans only the colour-visible slice.
    const auto recordChams = [&]() {
        for (Uint32 i = 0; i < visibleInstanceCount_ && i < frameInstances_.size(); ++i) {
            if (frameInstances_[i].materialId == 1) {
                chamsInstanceIndex_ = static_cast<int>(i);
                break;
            }
        }
    };

    // Without an installed rig / a valid bounding sphere we have nothing to
    // size the cull with — submit everything untouched and treat it as visible.
    if (!rigInstalled_ || numJoints_ <= 0 || rigBoundingRadius_ <= 0.0f) {
        framePalette_ = palette;
        frameInstances_ = instances;
        visibleInstanceCount_ = static_cast<Uint32>(instances.size());
        frameDirty_ = !instances.empty();
        recordChams();
        return;
    }

    framePalette_.reserve(palette.size());
    frameInstances_.reserve(instances.size());

    // Bounding sphere of an instance: the rig-local sphere pushed through the
    // instance world transform.  Centre tracks the actual rendered mesh (which
    // is vertically offset from the sim position), and the radius is scaled by
    // the transform's largest axis so the test stays conservative for any
    // (uniform or not) scale.
    const auto onScreen = [&](const SkinnedInstance& inst) {
        const glm::vec3 center = glm::vec3(inst.worldTransform * glm::vec4(rigBoundingCenter_, 1.0f));
        const float sx = glm::length(glm::vec3(inst.worldTransform[0]));
        const float sy = glm::length(glm::vec3(inst.worldTransform[1]));
        const float sz = glm::length(glm::vec3(inst.worldTransform[2]));
        const float radius = rigBoundingRadius_ * std::max({sx, sy, sz});
        return sphereInFrustum(center, radius, frustum);
    };

    // Copy one source instance into the frame buffers, compacting its palette
    // slice.  Returns false (and copies nothing) if the slice is out of range.
    const auto append = [&](const SkinnedInstance& inst) {
        const size_t base = inst.paletteBase;
        if (base + static_cast<size_t>(numJoints_) > palette.size())
            return false;
        SkinnedInstance copy = inst;
        copy.paletteBase = static_cast<uint32_t>(framePalette_.size());
        framePalette_.insert(framePalette_.end(),
                             palette.begin() + static_cast<std::ptrdiff_t>(base),
                             palette.begin() + static_cast<std::ptrdiff_t>(base) + numJoints_);
        frameInstances_.push_back(copy);
        return true;
    };

    // Pass 1: on-screen instances fill the front of the buffer (colour pass
    // draws exactly this slice).  Pass 2: off-screen instances follow them so
    // the shadow pass — which draws the whole buffer — still sees them and they
    // keep casting shadows even when the player is off the edge of the screen.
    for (const SkinnedInstance& inst : instances) {
        if (onScreen(inst) && append(inst))
            ++visibleInstanceCount_;
    }
    for (const SkinnedInstance& inst : instances) {
        if (!onScreen(inst))
            append(inst);
    }

    frameDirty_ = !frameInstances_.empty();
    recordChams();
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
    if (!growBuf(palettesSsboInfo_.ssbo_,
                 palettesSsboInfo_.capacityBytes_,
                 paletteBytes,
                 SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ))
        return false;
    if (!growBuf(instancesSsboInfo_.ssbo_,
                 instancesSsboInfo_.capacityBytes_,
                 instanceBytes,
                 SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ))
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
        SDL_UploadToGPUBuffer(copyPass, &s, &d, /*cycle=*/true);
    };
    upload(paletteXfer_, palettesSsboInfo_.ssbo_, framePalette_.data(), paletteBytes);
    upload(instanceXfer_, instancesSsboInfo_.ssbo_, frameInstances_.data(), instanceBytes);
}

// ─── Per-frame: draw ─────────────────────────────────────────────────────────

void SkinnedRenderer::draw(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
{
    // TODO(graphics): instanced GPU skinning draw call.  See `setFrame`
    // doc-block for the data layout and shader pseudocode.  Sketch:
    //
    // Colour pass renders only the on-screen instances (the front slice of the
    // buffer).  Off-screen instances live past visibleInstanceCount_ and are
    // drawn solely by drawDepth() so they still cast shadows.
    if (!rigInstalled_ || !pipeline_ || visibleInstanceCount_ == 0 || !palettesSsboInfo_.ssbo_ ||
        !instancesSsboInfo_.ssbo_)
    {
        return;
    }

    SDL_BindGPUGraphicsPipeline(renderPass, pipeline_);

    //   // The geometry pass already pushed view+projection at vertex UBO
    //   // slot 0 in NewRenderer::drawGeometryPass — no need to push again.
    //

    SDL_GPUBuffer* ssbos[2] = {palettesSsboInfo_.ssbo_, instancesSsboInfo_.ssbo_};
    SDL_BindGPUVertexStorageBuffers(renderPass, 0, ssbos, 2);
    for (auto sm : skinnedMeshes_) {
        if (!sm.vb || !sm.boneVb || !sm.ib) {
            continue;
        }
        std::vector<SDL_GPUBufferBinding> vertexBufferBindings;

        SDL_GPUBufferBinding palleteBufferBinding{
            .buffer = sm.vb,
            .offset = 0,
        };
        vertexBufferBindings.push_back(palleteBufferBinding);

        SDL_GPUBufferBinding instanceBufferBinding{
            .buffer = sm.boneVb,
            .offset = 0,
        };
        vertexBufferBindings.push_back(instanceBufferBinding);

        SDL_BindGPUVertexBuffers(renderPass, 0, vertexBufferBindings.data(), 2);

        SDL_GPUBufferBinding indexBufferBinding{
            .buffer = sm.ib,
            .offset = 0,
        };

        SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(renderPass, sm.indexCount, visibleInstanceCount_, 0, 0, 0);
    }

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

void SkinnedRenderer::drawChams(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* /*cmd*/)
{
    if (!rigInstalled_ || !chamsPipeline_ || chamsInstanceIndex_ < 0 || !palettesSsboInfo_.ssbo_ ||
        !instancesSsboInfo_.ssbo_)
    {
        return;
    }
    if (static_cast<size_t>(chamsInstanceIndex_) >= frameInstances_.size())
        return;

    SDL_BindGPUGraphicsPipeline(renderPass, chamsPipeline_);

    // Same view-projection UBO the geometry pass already pushed at vertex slot 0.
    SDL_GPUBuffer* ssbos[2] = {palettesSsboInfo_.ssbo_, instancesSsboInfo_.ssbo_};
    SDL_BindGPUVertexStorageBuffers(renderPass, 0, ssbos, 2);
    for (auto sm : skinnedMeshes_) {
        if (!sm.vb || !sm.boneVb || !sm.ib)
            continue;
        SDL_GPUBufferBinding vertexBufferBindings[2] = {
            {.buffer = sm.vb, .offset = 0},
            {.buffer = sm.boneVb, .offset = 0},
        };
        SDL_BindGPUVertexBuffers(renderPass, 0, vertexBufferBindings, 2);

        SDL_GPUBufferBinding indexBufferBinding{.buffer = sm.ib, .offset = 0};
        SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        // Draw just the flagged killer instance (gl_InstanceIndex = first_instance).
        SDL_DrawGPUIndexedPrimitives(
            renderPass, sm.indexCount, 1, 0, 0, static_cast<Uint32>(chamsInstanceIndex_));
    }
}

void SkinnedRenderer::drawDepth(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* /*cmd*/)
{
    if (!rigInstalled_ || !depthPipeline_ || frameInstances_.empty() || !palettesSsboInfo_.ssbo_ ||
        !instancesSsboInfo_.ssbo_)
    {
        return;
    }

    SDL_BindGPUGraphicsPipeline(renderPass, depthPipeline_);

    // The caller (NewRenderer::drawGeometryDepthPass) already pushed the
    // shadow view-projection at vertex UBO slot 0.
    //
    // Unlike the colour pass, the shadow pass draws the ENTIRE instance buffer
    // (frameInstances_.size(), not just visibleInstanceCount_) so that players
    // off the edge of the screen still cast shadows into the view.

    SDL_GPUBuffer* ssbos[2] = {palettesSsboInfo_.ssbo_, instancesSsboInfo_.ssbo_};
    SDL_BindGPUVertexStorageBuffers(renderPass, 0, ssbos, 2);
    for (auto sm : skinnedMeshes_) {
        if (!sm.vb || !sm.boneVb || !sm.ib) {
            continue;
        }
        SDL_GPUBufferBinding vertexBufferBindings[2] = {
            {.buffer = sm.vb, .offset = 0},
            {.buffer = sm.boneVb, .offset = 0},
        };
        SDL_BindGPUVertexBuffers(renderPass, 0, vertexBufferBindings, 2);

        SDL_GPUBufferBinding indexBufferBinding{
            .buffer = sm.ib,
            .offset = 0,
        };
        SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(renderPass, sm.indexCount, frameInstances_.size(), 0, 0, 0);
    }
}

bool SkinnedRenderer::createSkinningPipeline(SDL_GPUTextureFormat& colorTarget, const SDL_GPUShaderFormat& shaderFormat)
{
    Boilerplate::ShaderInfo vertexShader{};
    vertexShader.path = "shaders-new/skinned_geometry.vert";
    vertexShader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexShader.uniformBufferCount = 1;
    vertexShader.storageBufferCount = 2;

    Boilerplate::ShaderInfo fragmentShader{};
    fragmentShader.path = "shaders-new/debug.frag";
    fragmentShader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;

    SDL_GPUVertexBufferDescription vertexBufferDescription{};
    vertexBufferDescription.slot = 0;
    vertexBufferDescription.pitch = (sizeof(ModelVertex));
    vertexBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBufferDescription.instance_step_rate = 0;

    SDL_GPUVertexBufferDescription vertexBoneInfluenceBufferDescription{};
    vertexBoneInfluenceBufferDescription.slot = 1;
    vertexBoneInfluenceBufferDescription.pitch = (sizeof(BoneInfluence));
    vertexBoneInfluenceBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBoneInfluenceBufferDescription.instance_step_rate = 0;

    Boilerplate::VertexInputLayout vertexLayout{};
    vertexLayout.bufferDescriptions.push_back(vertexBufferDescription);
    vertexLayout.bufferDescriptions.push_back(vertexBoneInfluenceBufferDescription);
    vertexLayout.attributes = {
        // Mesh Vertex
        Boilerplate::makeAttribute(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ModelVertex, position), 0),
        Boilerplate::makeAttribute(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ModelVertex, normal), 0),
        Boilerplate::makeAttribute(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ModelVertex, texCoord), 0),
        Boilerplate::makeAttribute(3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(ModelVertex, tangent), 0),

        // Bone Influence
        Boilerplate::makeAttribute(4, SDL_GPU_VERTEXELEMENTFORMAT_INT4, offsetof(BoneInfluence, boneIndices), 1),
        Boilerplate::makeAttribute(5, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(BoneInfluence, boneWeights), 1),
    };

    pipeline_ = Boilerplate::createGraphicsPipeline(
        device_, colorTarget, shaderFormat, vertexShader, fragmentShader, vertexLayout, true, true);

    return pipeline_ != nullptr;
}

bool SkinnedRenderer::createSkinnedDepthPipeline(const SDL_GPUShaderFormat& shaderFormat)
{
    Boilerplate::ShaderInfo vertexShader{};
    vertexShader.path = "shaders-new/skinned_geometry_depth.vert";
    vertexShader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexShader.uniformBufferCount = 1;
    vertexShader.storageBufferCount = 2;

    Boilerplate::ShaderInfo fragmentShader{};
    fragmentShader.path = "shaders-new/emtpy.frag";
    fragmentShader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;

    SDL_GPUVertexBufferDescription vertexBufferDescription{};
    vertexBufferDescription.slot = 0;
    vertexBufferDescription.pitch = (sizeof(ModelVertex));
    vertexBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBufferDescription.instance_step_rate = 0;

    SDL_GPUVertexBufferDescription vertexBoneInfluenceBufferDescription{};
    vertexBoneInfluenceBufferDescription.slot = 1;
    vertexBoneInfluenceBufferDescription.pitch = (sizeof(BoneInfluence));
    vertexBoneInfluenceBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBoneInfluenceBufferDescription.instance_step_rate = 0;

    Boilerplate::VertexInputLayout vertexLayout{};
    vertexLayout.bufferDescriptions.push_back(vertexBufferDescription);
    vertexLayout.bufferDescriptions.push_back(vertexBoneInfluenceBufferDescription);
    vertexLayout.attributes = {
        // Mesh Vertex
        Boilerplate::makeAttribute(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ModelVertex, position), 0),
        Boilerplate::makeAttribute(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ModelVertex, normal), 0),
        Boilerplate::makeAttribute(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ModelVertex, texCoord), 0),
        Boilerplate::makeAttribute(3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(ModelVertex, tangent), 0),

        // Bone Influence
        Boilerplate::makeAttribute(4, SDL_GPU_VERTEXELEMENTFORMAT_INT4, offsetof(BoneInfluence, boneIndices), 1),
        Boilerplate::makeAttribute(5, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(BoneInfluence, boneWeights), 1),
    };

    Boilerplate::PipelineDescription depthPipelineDesc{};
    depthPipelineDesc.vertexShaderInfo = &vertexShader;
    depthPipelineDesc.fragmentShaderInfo = &fragmentShader;
    depthPipelineDesc.shaderFormat = shaderFormat;
    depthPipelineDesc.vertexInputLayout = &vertexLayout;
    depthPipelineDesc.colorTarget = nullptr;
    depthPipelineDesc.depthTest = true;
    depthPipelineDesc.depthWrite = true;

    depthPipelineDesc.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    depthPipelineDesc.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    depthPipelineDesc.rasterizer_state.enable_depth_bias = true;
    depthPipelineDesc.rasterizer_state.depth_bias_constant_factor = 500.0f;
    depthPipelineDesc.rasterizer_state.depth_bias_slope_factor = 1.0f;
    depthPipelineDesc.rasterizer_state.depth_bias_clamp = 0.005f;

    depthPipeline_ = Boilerplate::createGraphicsDepthPipeline(device_, depthPipelineDesc);

    return depthPipeline_ != nullptr;
}

bool SkinnedRenderer::createChamsPipeline()
{
    // Same skinned vertex shader as the colour pass (reads palette + instances),
    // but a flat-red fragment shader and an inverted depth test so only the
    // wall-occluded fragments are drawn. Built by hand (not via Boilerplate)
    // because we need a GREATER compare op, depth-write OFF, and NO depth bias.
    Boilerplate::ShaderInfo vertexShaderInfo{};
    vertexShaderInfo.path = "shaders-new/skinned_geometry.vert";
    vertexShaderInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexShaderInfo.uniformBufferCount = 1;
    vertexShaderInfo.storageBufferCount = 2;

    Boilerplate::ShaderInfo fragmentShaderInfo{};
    fragmentShaderInfo.path = "shaders-new/chams.frag";
    fragmentShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;

    SDL_GPUShader* vertexShader = Boilerplate::loadShader(device_, vertexShaderInfo, shaderFormat_);
    SDL_GPUShader* fragmentShader = Boilerplate::loadShader(device_, fragmentShaderInfo, shaderFormat_);
    if (!vertexShader || !fragmentShader) {
        if (vertexShader)
            SDL_ReleaseGPUShader(device_, vertexShader);
        if (fragmentShader)
            SDL_ReleaseGPUShader(device_, fragmentShader);
        return false;
    }

    SDL_GPUVertexBufferDescription vertexBufferDescription{};
    vertexBufferDescription.slot = 0;
    vertexBufferDescription.pitch = sizeof(ModelVertex);
    vertexBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexBufferDescription boneBufferDescription{};
    boneBufferDescription.slot = 1;
    boneBufferDescription.pitch = sizeof(BoneInfluence);
    boneBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    std::vector<SDL_GPUVertexBufferDescription> bufferDescriptions = {vertexBufferDescription, boneBufferDescription};
    std::vector<SDL_GPUVertexAttribute> attributes = {
        Boilerplate::makeAttribute(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ModelVertex, position), 0),
        Boilerplate::makeAttribute(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ModelVertex, normal), 0),
        Boilerplate::makeAttribute(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ModelVertex, texCoord), 0),
        Boilerplate::makeAttribute(3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(ModelVertex, tangent), 0),
        Boilerplate::makeAttribute(4, SDL_GPU_VERTEXELEMENTFORMAT_INT4, offsetof(BoneInfluence, boneIndices), 1),
        Boilerplate::makeAttribute(5, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(BoneInfluence, boneWeights), 1),
    };

    SDL_GPUVertexInputState vertexInputState{};
    vertexInputState.num_vertex_buffers = static_cast<Uint32>(bufferDescriptions.size());
    vertexInputState.vertex_buffer_descriptions = bufferDescriptions.data();
    vertexInputState.num_vertex_attributes = static_cast<Uint32>(attributes.size());
    vertexInputState.vertex_attributes = attributes.data();

    SDL_GPUColorTargetDescription colorTargetDesc{};
    colorTargetDesc.format = colorFormat_;
    colorTargetDesc.blend_state = Boilerplate::OVER_BLEND_MODE;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertexShader;
    info.fragment_shader = fragmentShader;
    info.vertex_input_state = vertexInputState;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.target_info.color_target_descriptions = &colorTargetDesc;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    // GREATER: keep only fragments BEHIND the stored (world) depth → occluded.
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    chamsPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);

    SDL_ReleaseGPUShader(device_, vertexShader);
    SDL_ReleaseGPUShader(device_, fragmentShader);

    if (!chamsPipeline_)
        std::cerr << "SkinnedRenderer: failed to create chams pipeline\n";
    return chamsPipeline_ != nullptr;
}
