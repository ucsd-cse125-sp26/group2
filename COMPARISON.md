# OpenGL vs SDL3 Render Pipeline — in the Context of ECS

---

## First: What ECS Needs From a Renderer

ECS is built on these principles:
- **Batch everything** — process 1000 entities of the same type at once
- **No shared mutable state** — systems don't step on each other
- **Data-driven** — components are plain data, systems read them linearly
- **Order independence** — systems shouldn't care about what ran before

---

## OpenGL's Pipeline: The State Machine Problem

OpenGL is a **global state machine**. Every call mutates a hidden global context. The pipeline looks like this:

```
┌─────────────────────────────────────────────────────┐
│              OPENGL GLOBAL STATE (hidden)           │
│  current VAO | current shader | current texture     │
│  blend mode  | depth test     | stencil state ...   │
└─────────────────────────────────────────────────────┘
        ↑ every call reads/writes this

CPU: glBind* → glUniform* → glDraw* → glBind* → ...
              (mutate)     (draw)    (mutate)
```

### The full OpenGL draw loop for ECS entities:

```c
// RenderSystem iterates entities with [Mesh, Transform, Material]
for each entity in render_query:

    // 1. BIND shader (mutates global state)
    glUseProgram(entity.material.shader);

    // 2. BIND VAO (mutates global state)
    glBindVertexArray(entity.mesh.vao);

    // 3. BIND textures (mutates global state — every slot!)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, entity.material.albedo);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, entity.material.normal);

    // 4. SET uniforms (mutates global state)
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, entity.transform.matrix);
    glUniformMatrix4fv(loc_view,  1, GL_FALSE, camera.view);
    glUniform3fv(loc_color, 1, entity.material.color);

    // 5. DRAW (finally)
    glDrawElements(GL_TRIANGLES, entity.mesh.index_count, GL_UNSIGNED_INT, 0);

    // State is now "dirty" — next entity must re-bind everything
    // or it inherits whatever state was left behind
```

### Why this is hostile to ECS:

**Problem 1: Global state bleeds between systems**
```c
// ParticleSystem runs before RenderSystem
void ParticleSystem::update() {
    glUseProgram(particle_shader);      // sets global shader
    glBindVertexArray(particle_vao);    // sets global VAO
    // ...draws particles...
}

// RenderSystem now runs — state is dirty from ParticleSystem
// Every entity MUST rebind everything or risk inheriting wrong state
// There's no isolation — systems share the same global GPU context
void RenderSystem::update() {
    for each entity:
        glUseProgram(...);      // MUST re-set — can't trust current state
        glBindVertexArray(...); // MUST re-set
        // ...
}
```

**Problem 2: No batching without manual sorting**
```c
// To batch-draw 500 enemy entities with the same mesh+shader,
// you must manually sort your ECS query results first:

// Without sorting (naive ECS iteration):
// bind shader_A → draw entity1
// bind shader_B → draw entity2   ← shader switch! expensive
// bind shader_A → draw entity3   ← switch back! wasted
// bind shader_A → draw entity4
// = O(n) state changes

// With sorting:
// Sort by [shader, mesh, material] first — extra CPU work your ECS
// architecture didn't need before the renderer demanded it
```

**Problem 3: Uniforms per-draw = CPU bottleneck**
```c
// Each entity needs its transform uploaded individually
// 1000 entities = 1000 glUniformMatrix4fv calls
// Each call crosses the CPU→GPU barrier

// UBOs help but require manual management:
glBindBuffer(GL_UNIFORM_BUFFER, ubo);
glBufferSubData(GL_UNIFORM_BUFFER, offset, size, &transform); // per entity
```

**Problem 4: Multi-threaded recording is impossible**
```c
// OpenGL context is bound to ONE thread
// Your ECS parallel systems cannot record draw calls concurrently:
// Thread A: physics system   ✅
// Thread B: animation system ✅
// Thread C: render system    ← ONLY this thread can touch OpenGL ❌
//           all rendering must funnel through one thread
```

---

## SDL3's Pipeline (SDL_GPU): The Command Buffer Model

SDL_GPU is a **modern explicit API**. No global state. Commands are recorded into a buffer, then submitted.

```
┌────────────────────────────────────────────────────────────┐
│                  NO GLOBAL GPU STATE                       │
│  Everything is explicit, local, and self-contained         │
└────────────────────────────────────────────────────────────┘

CPU Thread A: [CommandBuffer A] record... record... record...─┐
CPU Thread B: [CommandBuffer B] record... record... record...─┤→ Submit → GPU
CPU Thread C: [CommandBuffer C] record... record... record...─┘
```

### The full SDL_GPU draw loop for ECS entities:

```c
// FRAME START — acquire command buffer (one per frame, or per thread)
SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(device);

SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmdbuf, &color_target, 1, NULL);

    // Bind pipeline ONCE for all entities sharing it (sorted by system)
    SDL_BindGPUGraphicsPipeline(pass, mesh_pipeline);

    // Upload ALL transforms at once as a structured buffer
    // (ECS gives us contiguous component arrays — perfect for this)
    SDL_GPUBufferBinding transforms_binding = {
        .buffer = transforms_gpu_buffer,  // all 1000 entity transforms
        .offset = 0
    };
    SDL_BindGPUVertexStorageBuffers(pass, 0, &transforms_binding, 1);

    // ONE draw call for 1000 identical entities = instanced rendering
    SDL_DrawGPUPrimitivesIndirect(pass, draw_buffer, 0, entity_count);

SDL_EndGPURenderPass(pass);

// FRAME END — submit everything at once
SDL_SubmitGPUCommandBuffer(cmdbuf);
```

---

## Side-by-Side Pipeline Comparison

```
OPENGL PIPELINE                    SDL_GPU PIPELINE
─────────────────────────────────  ──────────────────────────────────
Frame start                        Frame start
    │                                  │
    ▼                                  ▼
[Mutate global state]              [Acquire CommandBuffer]
glUseProgram(shader)                   │ (no global state changed)
glBindVertexArray(vao)                 │
glBindTexture(...)                     ▼
    │                              [Begin RenderPass]
    ▼                              SDL_BeginGPURenderPass(cmdbuf, ...)
[Draw call]                            │
glDrawElements(...)                    ▼
    │                              [Bind Pipeline — ONCE]
    ▼                              SDL_BindGPUGraphicsPipeline(pass, pipeline)
[Mutate global state again]            │
glUseProgram(next_shader) ← leak?      ▼
glBindVertexArray(next_vao)        [Bind Resources — ONCE per batch]
    │                              SDL_BindGPUVertexBuffers(...)
    ▼                              SDL_BindGPUFragmentSamplers(...)
[Next draw call]                       │
glDrawElements(...)                    ▼
    │                              [Draw ALL instances]
[Repeat per entity]  ← n calls    SDL_DrawGPUPrimitivesIndirect(...)
    │                                  │  ← 1 call for n entities
    ▼                                  ▼
Frame end                          [End RenderPass]
SDL_GL_SwapWindow(win)             SDL_EndGPURenderPass(pass)
                                       │
                                       ▼
                                   [Submit]
                                   SDL_SubmitGPUCommandBuffer(cmdbuf)
```

---

## How Each ECS Pattern Maps to Each API

### Pattern 1: Batch Rendering (Same mesh, 1000 entities)

```c
// ─── OPENGL ─────────────────────────────────────────────
// Best case (if you manually sorted your ECS query):
glUseProgram(shader);        // 1 bind
glBindVertexArray(vao);      // 1 bind
for (int i = 0; i < 1000; i++) {
    glUniformMatrix4fv(model_loc, 1, GL_FALSE, transforms[i]);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, 0);
}
// = 1000 uniform uploads + 1000 draw calls
// CPU is bottleneck. Driver overhead per draw call is real.

// ─── SDL_GPU ─────────────────────────────────────────────
// ECS component arrays ARE the GPU buffer — upload once:
SDL_UploadToGPUBuffer(copy_pass, &src, &transforms_buffer, false);

// Bind pipeline + buffer once, draw all instances in ONE call:
SDL_BindGPUGraphicsPipeline(pass, pipeline);
SDL_BindGPUVertexStorageBuffers(pass, 0, &transforms_binding, 1);
SDL_DrawGPUPrimitives(pass, index_count, 1000, 0, 0);
//                                        ^^^^ instance count
// = 1 draw call. GPU reads transform[gl_InstanceIndex] in shader.
```

### Pattern 2: Multiple Independent Render Systems

```c
// ─── OPENGL ─────────────────────────────────────────────
// Systems CANNOT be isolated — they share the GL context state

void MeshRenderSystem::run() {
    glUseProgram(mesh_shader);   // mutates global
    // draw meshes...
    // leaves global state dirty
}

void ParticleSystem::run() {
    // What's the current shader? VAO? Blend mode?
    // We don't know — must defensively re-bind EVERYTHING
    glUseProgram(particle_shader);
    glEnable(GL_BLEND);                     // mutates global
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    // draw particles...
    glDisable(GL_BLEND);                    // must restore for next system!
}

void UIRenderSystem::run() {
    // Again — defensive re-bind of everything
    glDisable(GL_DEPTH_TEST);               // mutates global
    // ...
    glEnable(GL_DEPTH_TEST);               // must restore!
}
// = Systems are tightly coupled through shared global state

// ─── SDL_GPU ─────────────────────────────────────────────
// Each system records into its OWN section — zero state leakage

void MeshRenderSystem::record(SDL_GPURenderPass *pass) {
    SDL_BindGPUGraphicsPipeline(pass, mesh_pipeline);
    // pipeline contains ALL state: depth, blend, shader, etc.
    // completely self-contained
}

void ParticleSystem::record(SDL_GPURenderPass *pass) {
    SDL_BindGPUGraphicsPipeline(pass, particle_pipeline);
    // different pipeline — its own blend state baked in
    // MeshRenderSystem's state is untouched
}

void UIRenderSystem::record(SDL_GPURenderPass *pass) {
    SDL_BindGPUGraphicsPipeline(pass, ui_pipeline);
    // its own depth settings baked in
    // completely isolated
}
// = Systems are fully decoupled
```

### Pattern 3: Parallel Command Recording (ECS parallel systems)

```c
// ─── OPENGL ─────────────────────────────────────────────
// Impossible. One context, one thread.
// Your parallel ECS systems must funnel ALL render work
// through a single render thread. Parallelism is wasted.

// ─── SDL_GPU ─────────────────────────────────────────────
// Each worker thread records its own command buffer:

// Thread A — MeshRenderSystem
SDL_GPUCommandBuffer *cmdbuf_a = SDL_AcquireGPUCommandBuffer(device);
// ... record mesh draws ...

// Thread B — ParticleSystem (runs IN PARALLEL with Thread A)
SDL_GPUCommandBuffer *cmdbuf_b = SDL_AcquireGPUCommandBuffer(device);
// ... record particle draws ...

// Main thread — submit both in order
SDL_SubmitGPUCommandBuffer(cmdbuf_a);
SDL_SubmitGPUCommandBuffer(cmdbuf_b);
// GPU executes them in submission order
```

---

## The Core Architectural Truth

```
┌─────────────────────────────────────────────────────────┐
│  OpenGL thinks in OBJECTS (bind this, draw that)        │
│  ECS thinks in ARRAYS (process all of these)            │
│                                                         │
│  SDL_GPU thinks in COMMANDS (record now, execute later) │
│  ECS thinks in SYSTEMS (collect, process, submit)       │
│                                                         │
│  SDL_GPU and ECS share the same mental model.           │
│  OpenGL and ECS are philosophically opposed.            │
└─────────────────────────────────────────────────────────┘
```

| Aspect            | OpenGL             | SDL3 GPU           | ECS Needs       |
|-------------------|--------------------|--------------------|-----------------|
| State model       | Global, mutable    | Explicit, local    | Local           |
| Draw model        | Immediate per-object | Recorded, batched | Batched        |
| Instancing        | Manual + UBOs      | First-class        | First-class     |
| System isolation  | Shared state       | Per-pipeline       | Isolated        |
| Parallelism       | Single thread      | Multi-thread       | Parallel        |
| Data layout       | Object-oriented    | Buffer arrays      | Array/SoA       |
| CPU overhead      | Per draw call      | Per submission     | Per batch       |
