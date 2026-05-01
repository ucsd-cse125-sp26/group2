/// @file Game.cpp
/// @brief Implementation of the top-level Game class and SDL application callbacks.

#include "Game.hpp"

#include "SDL3/SDL_init.h"
#include "animation/CharacterAnimator.hpp"
#include "ecs/AssetCatalog.hpp"
#include "ecs/components/AnimatedCharacter.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Controllable.hpp"
#include "ecs/components/DeathInfo.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerSimState.hpp" // also pulls in PlayerVisState
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/ViewmodelConfig.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/systems/HitboxSystem.hpp"
#include "hud/debug/HudDebugPanel.hpp"
#include "network/NetworkConfig.hpp"
#include "network/ShotEvent.hpp"
#include "particles/ParticleEvents.hpp"
#include "renderer/GlowCylinder.hpp"
#include "renderer/GlowSphere.hpp"
#include "renderer/GraphicsConfig.hpp"
#include "systems/InputSampleSystem.hpp"
#include "systems/InputSendSystem.hpp"
#include "systems/PredictionSystem.hpp"
#include "systems/ReconciliationSystem.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <numeric>

namespace
{
int addAssetDefinition(AssetRegistry& assets, const AssetDefinition& def)
{
    return assets.add(
        def.name, def.filename, def.role, def.renderScale, def.renderTranslation, def.renderRotationDegrees);
}

glm::quat assetRotation(const AssetEntry& asset)
{
    const glm::vec3 r = glm::radians(asset.renderRotationDegrees);
    return glm::angleAxis(r.y, glm::vec3{0.0f, 1.0f, 0.0f}) * glm::angleAxis(r.x, glm::vec3{1.0f, 0.0f, 0.0f}) *
           glm::angleAxis(r.z, glm::vec3{0.0f, 0.0f, 1.0f});
}
} // namespace

bool Game::init()
{
#ifdef USE_HYBRID_RENDERER
    static constexpr const char* k_appName = "group2";
#else
    static constexpr const char* k_appName = "client";
#endif
    SDL_SetAppMetadata(k_appName, "0.1.0", "com.cse125.group2");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!NET_Init()) {
        SDL_Log("NET_Init() failed: %s", SDL_GetError());
        return false;
    }

    {
        const char* base = SDL_GetBasePath();
        std::string cfgPath = std::string(base ? base : "") + "config.toml";
        netCfg = loadNetworkConfig(cfgPath.c_str());

        // Apply graphics backend selection BEFORE SDL_CreateGPUDevice runs in
        // Renderer::init.  SDL_GPU honours SDL_HINT_GPU_DRIVER at device
        // creation; if the requested driver is unavailable SDL falls back to
        // another supported one automatically.
        const GraphicsConfig gfxCfg = loadGraphicsConfig(cfgPath.c_str());
        if (const char* driver = gpuBackendHintString(gfxCfg.backend))
            SDL_SetHint(SDL_HINT_GPU_DRIVER, driver);
    }

    window = SDL_CreateWindow(k_appName, 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    // DebugUI must be initialised before Renderer — it creates the ImGui
    // context that the GPU render backend (in Renderer::init) requires.
    if (!debugUI.init(window)) {
        SDL_Log("DebugUI init failed");
        SDL_DestroyWindow(window);
        return false;
    }

    if (!renderer.init(window)) {
        SDL_Log("Renderer init failed");
        debugUI.shutdown();
        SDL_DestroyWindow(window);
        return false;
    }

    // Particle system needs the device + formats from the renderer.
    // colorFmt must match the render target particles draw into (HDR = RGBA16F),
    // NOT the swapchain format.  shaderFmt must be the single format the
    // renderer selected, not the bitmask of all supported formats.
    if (!particleSystem.init(renderer.getDevice(), Renderer::getHdrFormat(), renderer.getShaderFormat())) {
        SDL_Log("ParticleSystem init failed (non-fatal — particles disabled)");
    } else {
        renderer.setParticleSystem(&particleSystem);

        // Wire dispatcher events to particle system.
        // NOTE: WeaponFiredEvent is NOT wired here — local weapon VFX (tracers,
        // beams, impacts) are spawned explicitly in iterate() so we control
        // exactly which effect plays per weapon type.  onWeaponFired would
        // unconditionally spawn a hitscan beam for every hitscan weapon.
        dispatcher.sink<ProjectileImpactEvent>().connect<&ParticleSystem::onImpact>(particleSystem);
        dispatcher.sink<ExplosionEvent>().connect<&ParticleSystem::onExplosion>(particleSystem);
    }

    // Sound effects system — initialised after particles so audio can mirror the
    // same event-driven pattern.  Failure is non-fatal: the game runs silently.
    if (!sfxSystem.init()) {
        SDL_Log("[client] SfxSystem init failed (non-fatal — sound effects disabled)");
    } else {
        // WeaponFiredEvent: play the weapon fire sound for every shot.
        dispatcher.sink<WeaponFiredEvent>().connect<&SfxSystem::onWeaponFired>(sfxSystem);
        // ExplosionEvent: also play the explosion SFX alongside the particle effect.
        dispatcher.sink<ExplosionEvent>().connect<&SfxSystem::onExplosion>(sfxSystem);
    }

    // HUD system — needs device + shader format from renderer, SDF atlas from particles.
    if (particleSystem.sdfReady()) {
        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        if (!hud_.init(renderer.getDevice(),
                       renderer.getShaderFormat(),
                       particleSystem.sdfAtlas(),
                       static_cast<uint32_t>(winW),
                       static_cast<uint32_t>(winH)))
        {
            SDL_Log("Hud init failed (non-fatal — HUD disabled)");
        } else {
            renderer.setHudTexture(hud_.getOutputTexture());
        }
    }

    // ── Load map ──────────────────────────────────────────────────────────
    // Scale: the map was authored in meters; the game uses Quake units (inches).
    // 1 m = 39.3701 in.
    {
        // Toggle: does this map use SEPARATED collision and visual meshes?
        //   false → "prototype mode": every mesh in the GLB is both visual *and*
        //           collision (used for blockout maps like map1.glb).
        //   true  → "separated mode": collision-only meshes are tagged in Blender
        //           (e.g. "COL_*" prefix) and excluded from the visual model.
        //           Used for production maps where collision is hand-authored
        //           independently from rendering geometry.
        static constexpr bool k_separatedCollisionMap = false;

        // Pattern that identifies collision-only nodes when in separated mode.
        // The Blender export convention here is a "COL_" prefix; nodes whose
        // names contain this substring (case-insensitive) are treated as
        // collision-only and stripped from the visual mesh list.
        static constexpr const char* k_collisionPattern = "COL_";

        // In separated mode, should the loader auto-fit each collision mesh
        // to a primitive (AABB / cylinder / sphere / brush) — or load the raw
        // Blender geometry as-is (triangle mesh)?
        //   true  (default) → auto-detect: convex shapes become cheap primitives
        //                     or brushes; only truly non-convex meshes fall
        //                     back to triMesh.  Smoother collision overall.
        //   false           → trust the artist: every collision mesh stays a
        //                     triMesh, vertex-for-vertex, exactly as authored.
        static constexpr bool k_guessShapesProcessed = true;

        const char* const mapFilename =
            k_separatedCollisionMap ? "maps/map1_script_collisions.glb" : kMapAsset.filename;

        const char* base = SDL_GetBasePath();
        const std::string mapPath = std::string(base ? base : "") + "assets/" + mapFilename;

        // 1) Extract collision geometry.
        physics::MapLoadOptions opts;
        opts.scale = kMapAsset.loadScale;
        opts.allMeshesAreCollision = !k_separatedCollisionMap;
        // In separated mode, recognise collision-prefixed nodes; in prototype
        // mode, the field is unused (every mesh is collision by definition).
        if (k_separatedCollisionMap)
            opts.collisionCollection = k_collisionPattern;
        opts.guessShapesProcessed = k_guessShapesProcessed;
        opts.addFloorPlane = false; // Map geometry provides its own floor.

        if (physics::loadMapCollision(mapPath, mapCollision_, opts)) {
            SDL_Log("[client] map collision loaded: %zu planes, %zu boxes, %zu brushes, %zu cylinders, %zu spheres, "
                    "%zu trimeshes",
                    mapCollision_.planes.size(),
                    mapCollision_.boxes.size(),
                    mapCollision_.brushes.size(),
                    mapCollision_.cylinders.size(),
                    mapCollision_.spheres.size(),
                    mapCollision_.triMeshes.size());
        } else {
            SDL_Log("[client] WARNING: map collision load failed — falling back to testWorld()");
        }

        // 2) Load visual model for rendering (scene-pass so it draws as static world geometry).
        // In separated mode, exclude collision-only nodes so they aren't rendered.
        const std::string visualExclude = k_separatedCollisionMap ? std::string(k_collisionPattern) : std::string();
        const int mapId = addAssetDefinition(assets_, kMapAsset);
        const int mapModelIdx = renderer.loadSceneModel(
            mapFilename, kMapAsset.loadTranslation, kMapAsset.loadScale, kMapAsset.flipUVs, visualExclude);
        assets_.setModelIndex(mapId, mapModelIdx);
        if (mapModelIdx >= 0) {
            renderer.setModelScenePass(mapModelIdx, true);
            SDL_Log("[client] map visual loaded (model index %d, exclude='%s')", mapModelIdx, visualExclude.c_str());
        } else {
            SDL_Log("[client] WARNING: map visual load failed — map will be invisible");
        }
    }

    // ── Load props (render + collision) ───────────────────────────────────
    // These are standalone GLB models placed at fixed world positions.
    // Both visual and collision are loaded so players/projectiles interact.
    {
        const char* base = SDL_GetBasePath();
        const std::string basePath = base ? base : "";

        // Helper: load a prop with render + collision in one call.
        // `def.decomposeCollision = true` runs V-HACD on each non-convex sub-mesh
        // so organic/detailed shapes (a bottle, a metallic pallet) end up as a
        // handful of `WorldBrush`es instead of a giant triangle mesh.  Costs
        // a few seconds of load time per prop but kills triMesh edge-jitter.
        auto loadProp = [&](const AssetDefinition& def) {
            const int id = addAssetDefinition(assets_, def);
            const int modelIdx = renderer.loadSceneModel(def.filename, def.loadTranslation, def.loadScale, def.flipUVs);
            assets_.setModelIndex(id, modelIdx);
            if (modelIdx >= 0) {
                renderer.setModelScenePass(modelIdx, true);
            }

            // Load collision at the same position/scale.
            const std::string fullPath = basePath + "assets/" + def.filename;
            if (physics::loadPropCollision(
                    fullPath, mapCollision_, def.loadTranslation, def.loadScale, def.decomposeCollision))
            {
                assets_.setHasCollision(id);
            }
        };

        for (const AssetDefinition& def : kPropAssets)
            loadProp(def);

        // Update the active world with the new collision data (map + all props).
        physics::setActiveWorld(mapCollision_.geometry());
    }

    // ── Load entity models (render only, drawn via EntityRenderCmd) ──────
    {
        const int id = addAssetDefinition(assets_, kWraithAsset);
        wraithModelIdx = renderer.loadSceneModel(
            kWraithAsset.filename, kWraithAsset.loadTranslation, kWraithAsset.loadScale, kWraithAsset.flipUVs);
        assets_.setModelIndex(id, wraithModelIdx);
        if (wraithModelIdx < 0)
            SDL_Log("[client] WARNING: Wraith model failed to load — player model will be invisible");
    }

    // Load all weapon models (per WeaponType)
    {
        for (std::size_t i = 0; i < kWeaponAssets.size(); ++i) {
            const AssetDefinition& def = kWeaponAssets[i];
            if (def.filename) {
                const int id = addAssetDefinition(assets_, def);
                weaponAssetIds_[i] = id;
                weaponModelIndices_[i] =
                    renderer.loadSceneModel(def.filename, def.loadTranslation, def.loadScale, def.flipUVs);
                assets_.setModelIndex(id, weaponModelIndices_[i]);
                if (weaponModelIndices_[i] < 0)
                    SDL_Log("[client] WARNING: weapon model '%s' failed to load", def.filename);
            }
        }
    }

    // ── Procedural effects ──────────────────────────────────────────────
    {
        LoadedModel sphereModel = createGlowSphere(32, 32, 30.0f, glm::vec3(10.0f, 6.0f, 2.0f));
        const int id = addAssetDefinition(assets_, kEffectAssets[0]);
        glowSphereModelIdx_ = renderer.uploadSceneModel(sphereModel);
        assets_.setModelIndex(id, glowSphereModelIdx_);
    }
    {
        LoadedModel sphereModel = createGlowSphere(24, 24, 15.0f, glm::vec3(4.0f, 8.0f, 12.0f));
        const int id = addAssetDefinition(assets_, kEffectAssets[1]);
        movableSphereModelIdx_ = renderer.uploadSceneModel(sphereModel);
        assets_.setModelIndex(id, movableSphereModelIdx_);
    }
    {
        LoadedModel cylModel = createGlowCylinder(24, 1, glm::vec3(8.0f, 2.0f, 10.0f));
        const int id = addAssetDefinition(assets_, kEffectAssets[2]);
        glowCylinderModelIdx_ = renderer.uploadSceneModel(cylModel);
        assets_.setModelIndex(id, glowCylinderModelIdx_);
    }

    // Log the full asset registry.
    SDL_Log("[client] Asset registry: %d entries", assets_.count());
    for (const auto& e : assets_.entries())
        SDL_Log("[client]   '%s' → model %d (role=%d, collision=%s)",
                e.name.c_str(),
                e.modelIndex,
                static_cast<int>(e.role),
                e.hasCollision ? "yes" : "no");

    // Remove Controllable when the local player dies (RespawnTimer added),
    // restore it when they respawn (RespawnTimer removed).
    registry.on_construct<RespawnTimer>().connect<[](entt::registry& reg, entt::entity e) {
        if (reg.all_of<LocalPlayer>(e))
            reg.remove<Controllable>(e);
    }>();
    registry.on_destroy<RespawnTimer>().connect<[](entt::registry& reg, entt::entity e) {
        if (reg.all_of<LocalPlayer>(e))
            reg.emplace_or_replace<Controllable>(e);
    }>();

    client.onLocalPlayerReady([this](entt::entity local) {
        registry.emplace<LocalPlayer>(local);
        registry.emplace<InputSnapshot>(local);
        // Phase 5a: PreviousPosition is now seeded by Client::dispatchMessage
        // for ALL entities with Position on first snapshot, including the
        // local player. Use emplace_or_replace here so this callback stays
        // idempotent regardless of whether the seed pass got there first.
        registry.emplace_or_replace<PreviousPosition>(local, registry.get<Position>(local).value);

        // Phase 5b: emplace PlayerSimState on the local player so runMovement
        // (which iterates `<..., PlayerSimState, ...>`) will process it
        // during client-side prediction. PlayerSimState is a server-only
        // component — remote players don't have it on the client, so the
        // view filter naturally narrows to just the local player.
        // Server's snapshots don't replicate PlayerSimState so this stays
        // entirely client-side; that means subtle timer fields can drift
        // between client/server — fixed by Phase 4b's owner-only stream
        // when it lands.
        registry.emplace_or_replace<PlayerSimState>(local);

        // Only add Controllable if the player is not already dead (edge case:
        // joining while mid-death on a long-running server).
        if (!registry.all_of<RespawnTimer>(local))
            registry.emplace<Controllable>(local);

        // Animator runs for local too — future gun-IK / hands-on-weapon work
        // needs an up-to-date upper-body pose even when the body is invisible.
        // Rendering is gated by animUI_.showLocalBody (third-person debug).
        attachAnimatedCharacter(local);

        SDL_Log("[client] local player entity assigned: %d", static_cast<int>(local));
    });

    client.onParticleEvent([this](const NetParticleEvent& evt, entt::entity localPlayer) {
        // Hitmarker SFX: local player's shot was confirmed by the server to have
        // hit an enemy (surface == Flesh).  This check runs BEFORE the skip-self
        // guard so the shooter still hears the hitmarker even though their own
        // particle VFX was already spawned client-side for instant feedback.
        if (evt.source == localPlayer && evt.effectType == ParticleEffectType::Impact &&
            evt.surfaceType == SurfaceType::Flesh)
        {
            if (sfxSystem.isInitialized())
                sfxSystem.play(SfxId::FleshHit);
            hitmarkerTimer_ = 0.25f; // show hitmarker for 250ms
            hitmarkerIsHeadshot_ = (evt.headshot != 0);
            hitmarkerShieldBreak_ = (evt.shieldBreak != 0);

            // Queue floating damage number at hit position.
            if (evt.damage > 0.f) {
                const bool isHeadshot = (evt.headshot != 0);
                const bool isShielded = (evt.hadArmor != 0);
                pendingDamageNumbers_.push_back({evt.pos1, evt.damage, isHeadshot, isShielded});

                // Damage accumulator: reset if target changed, accumulate otherwise.
                if (evt.target != accumTarget_) {
                    accumTarget_ = evt.target;
                    accumTotal_ = 0;
                }
                accumTotal_ += static_cast<int>(evt.damage + 0.5f);
                accumResetTimer_ = 2.0f; // reset after 2s of no hits
                // Track latest hit type for accumulator color.
                accumLastHitType_ = isHeadshot ? uint8_t{2} : (isShielded ? uint8_t{1} : uint8_t{0});
            }
        }

        // Skip own effects that were already spawned locally for instant feedback.
        // Exceptions that must NOT be skipped:
        //   - Charge weapons: local VFX is skipped; we rely on server events.
        //   - Explosions / Smoke: server-authoritative (not locally predicted).
        //   - Impacts: local prediction only spawns tracers; all impact VFX
        //     (sparks, blood, bullet holes) come from server so player hits
        //     always get the correct surface type and normal.
        if (evt.source == localPlayer) {
            const bool isChargeWeapon = getWeaponConfig(evt.weaponType).isCharge;
            const bool isServerAuthoritative = evt.effectType == ParticleEffectType::Explosion ||
                                               evt.effectType == ParticleEffectType::Smoke ||
                                               evt.effectType == ParticleEffectType::Impact;
            if (!isChargeWeapon && !isServerAuthoritative)
                return;

            // For charge weapons from self: also dispatch the weapon-fired event
            // so the shoot sound plays and recoil kicks.
            if (evt.effectType == ParticleEffectType::HitscanBeam) {
                WeaponFiredEvent wfe;
                wfe.type = evt.weaponType;
                wfe.origin = evt.pos1;
                wfe.direction = glm::normalize(evt.pos2 - evt.pos1);
                wfe.isHitscan = true;
                wfe.hitPos = evt.pos2;
                dispatcher.enqueue(wfe);
            }
        }

        // For local player's charge weapon: override beam origin with
        // viewmodel muzzle position so the lightning comes from the gun.
        glm::vec3 evtOrigin = evt.pos1;
        if (evt.source == localPlayer && evt.effectType == ParticleEffectType::HitscanBeam) {
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            evtOrigin = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;
        }

        switch (evt.effectType) {
        case ParticleEffectType::BulletTracer:
            particleSystem.spawnBulletTracer(evtOrigin, evt.pos2, evt.param);
            break;
        case ParticleEffectType::HitscanBeam:
            particleSystem.spawnHitscanBeam(evtOrigin, evt.pos2, evt.weaponType);
            break;
        case ParticleEffectType::Impact:
            particleSystem.spawnImpactEffect(evt.pos1, evt.pos2, evt.surfaceType, evt.weaponType);
            break;
        case ParticleEffectType::Explosion:
            particleSystem.spawnExplosion(evt.pos1, evt.param);
            // Dispatch ExplosionEvent so SfxSystem plays the explosion sound.
            {
                ExplosionEvent expl;
                expl.pos = evt.pos1;
                expl.blastRadius = evt.param;
                dispatcher.enqueue(expl);
            }
            break;
        case ParticleEffectType::Smoke:
            particleSystem.spawnSmoke(evt.pos1, evt.param);
            break;
        }
    });

    client.onMatchStateUpdate([this](const MatchStatePacket& packet) {
        currentMatchPhase = packet.phase;
        countdownTimer = packet.countdownTimer;
    });

    client.onKillEvent([this](const NetKillEvent& evt) {
        killFeed.insert(killFeed.begin(),
                        KillFeedEvent{
                            evt.killerId,
                            evt.victimId,
                        });

        // TODO: Specific handling for local player deaths (display enemy health)
    });

    // Initialize runtime 3P weapon params from defaults
    for (int i = 0; i < 4; ++i)
        tpWeaponParams_[i] = getThirdPersonWeaponParams(static_cast<WeaponType>(i));

    const NetworkAddress clientNet = netCfg.clientNetwork;
    if (!client.init(clientNet.host.c_str(), clientNet.port, netCfg.transport)) {
        SDL_Log("Failed to connect to server");
        particleSystem.quit();
        renderer.quit();
        debugUI.shutdown();
        SDL_DestroyWindow(window);
        return false;
    }

    // Grab the mouse into relative mode so camera look works immediately.
    SDL_SetWindowRelativeMouseMode(window, true);
    mouseCaptured = true;

    // Load the shared skinned-character rig (skeleton + bind pose + weights).
    // Loading a single FBX is enough — any file with matching skin data works;
    // we use standard_walk.fbx because it's guaranteed present for locomotion.
    {
        const char* base = SDL_GetBasePath();
        const std::string assetsDir = std::string(base ? base : "") + "assets/animations/";
        const std::string rigPath = assetsDir + "standard_walk.fbx";
        if (!charRig_.loadFromFBX(rigPath)) {
            SDL_Log("[client] WARNING: rig load failed — animated characters disabled");
        } else {
            SDL_Log("[client] rig loaded — %d joints, %zu mesh(es)", charRig_.numJoints(), charRig_.meshes().size());

            // Auto-calculate rig scale so the animated model matches the
            // player's standing hitbox height, and compute the vertical
            // offset so the model's feet sit at the bottom of the AABB.
            {
                float meshMinY = 0.0f;
                float meshMaxY = 1.0f;
                charRig_.verticalBounds(meshMinY, meshMaxY);
                rigMeshMinY_ = meshMinY;

                const float meshHeight = meshMaxY - meshMinY;
                const float targetHeight = 2.0f * tms::k_standingHalfHeight; // 72 units
                if (meshHeight > 0.001f) {
                    kRigScale_ = targetHeight / meshHeight;
                } else {
                    kRigScale_ = 1.0f;
                }
                // Offset: move the model so its feet (meshMinY) align with
                // the bottom of the standing AABB (pos.y - standingHalfHeight).
                // Translation is relative to the entity's Position (AABB centre),
                // so: translation.y = -halfHeight - meshMinY * scale.
                kRigVerticalOffset_ = -tms::k_standingHalfHeight - rigMeshMinY_ * kRigScale_;
                SDL_Log("[client] rig auto-scale: meshY=[%.1f, %.1f] height=%.1f -> scale=%.4f, vertOffset=%.1f",
                        static_cast<double>(meshMinY),
                        static_cast<double>(meshMaxY),
                        static_cast<double>(meshHeight),
                        static_cast<double>(kRigScale_),
                        static_cast<double>(kRigVerticalOffset_));
            }

            // Load every animation clip onto the shared skeleton.
            for (uint8_t i = 0; i < static_cast<uint8_t>(ClipId::_Count); ++i) {
                const ClipId id = static_cast<ClipId>(i);
                const std::string clipPath = assetsDir + clipFile(id);
                if (!animLibrary_.loadClipFromFBX(charRig_, id, clipPath)) {
                    SDL_Log("[client] WARNING: failed to load clip '%s'", clipName(id));
                    continue;
                }
                SDL_Log(
                    "[client] clip '%s' duration=%.2fs", clipName(id), static_cast<double>(animLibrary_.duration(id)));
            }

            // Install the shared rig on the renderer's skinned-character path
            // (perf Phase 1B).  All animated entities will draw via instanced
            // GPU skinning against this single GPU model — no per-entity
            // clones, no CPU LBS, no per-frame vertex re-uploads.
            {
                const auto& rigMeshes = charRig_.meshes();
                std::vector<std::vector<Renderer::SkinVertex>> skinPerMesh(rigMeshes.size());
                for (size_t m = 0; m < rigMeshes.size(); ++m) {
                    const auto& src = rigMeshes[m].skinWeights;
                    auto& dst = skinPerMesh[m];
                    dst.resize(src.size());
                    for (size_t v = 0; v < src.size(); ++v) {
                        for (int k = 0; k < 4; ++k) {
                            dst[v].boneIndices[k] = src[v].boneIndices[k];
                            dst[v].boneWeights[k] = src[v].weights[k];
                        }
                    }
                }
                if (!legacyRenderer().setSkinnedRig(charRig_.templateLoadedModel(), skinPerMesh, charRig_.numJoints()))
                    SDL_Log("[client] WARNING: setSkinnedRig failed — falling back to per-entity path");
            }
        }

        // Build and resolve hitbox definitions (client-side, for debug visualization).
        clientHitboxRig_ = HitboxRig::buildMixamoDefault();
        clientHitboxRig_.resolveIndices(charRig_.jointMap());
        {
            int resolved = 0;
            for (const auto& def : clientHitboxRig_.definitions)
                if (def.boneIndex >= 0)
                    ++resolved;
            SDL_Log("[client] hitbox rig: %zu definitions, %d resolved", clientHitboxRig_.definitions.size(), resolved);
        }
    }

    prevTime = SDL_GetPerformanceCounter();
    statsPrevTime = prevTime;

    // Apply the default frame-rate-limit setting now that the renderer is ready.
    applyFrameRateLimit();

    // Bench mode: BENCH_SECONDS=N runs the client for N seconds, then prints a
    // single-line FPS summary to stderr and quits.  Driven by
    // `scripts/perf-100bots.sh` for baseline + post-change measurements.
    if (const char* envBench = SDL_getenv("BENCH_SECONDS")) {
        const float seconds = std::strtof(envBench, nullptr);
        if (seconds > 0.0f) {
            benchSeconds_ = seconds;
            benchActive_ = true;
            benchStartTime_ = prevTime;
            // Reserve enough room for ~5000 fps × bench duration (worst case),
            // so push_back never reallocates inside the hot path.
            benchFrameTimesMs_.reserve(static_cast<size_t>(seconds * 5000.0f));
            // Drop the renderer's vsync limiter so the bench reflects raw client capacity.
            limitFPSToMonitor = false;
            renderer.setVSync(false);
            SDL_Log("[bench] running for %.1fs then exiting (warmup %.1fs)",
                    static_cast<double>(seconds),
                    static_cast<double>(k_benchWarmupSeconds));
        }
    }

    // Bench-only kill switches for post-processing passes.  Lets a single
    // bench script flag isolate per-pass cost without persisting changes.
    //
    //   BENCH_NO_POSTFX=1   — disable bloom + SSR + GTAO + volumetrics + TAA
    //   BENCH_NO_SHADOWS=1  — disable cascaded shadow map pass
    //   BENCH_NO_TAA=1      — disable TAA + motion vectors only
    //   BENCH_NO_BLOOM=1    — disable bloom only
    //   BENCH_NO_SSR=1      — disable SSR only
    //   BENCH_NO_GTAO=1     — disable SSAO/GTAO only
    //   BENCH_NO_VOL=1      — disable volumetric lighting only
    if (const char* v = SDL_getenv("BENCH_NO_POSTFX"); v && *v && *v != '0') {
        renderer.toggles.bloom = false;
        renderer.toggles.ssr = false;
        renderer.toggles.ssao = false;
        renderer.toggles.volumetrics = false;
        legacyRenderer().aaMode = AAMode::Off;
        SDL_Log("[bench] post-FX disabled (bloom + ssr + gtao + volumetrics + AA)");
    }
    if (const char* v = SDL_getenv("BENCH_NO_SHADOWS"); v && *v && *v != '0') {
        renderer.toggles.shadows = false;
        SDL_Log("[bench] shadows disabled");
    }
    if (const char* v = SDL_getenv("BENCH_NO_TAA"); v && *v && *v != '0') {
        legacyRenderer().aaMode = AAMode::Off;
        SDL_Log("[bench] TAA disabled");
    }
    if (const char* v = SDL_getenv("BENCH_NO_BLOOM"); v && *v && *v != '0') {
        renderer.toggles.bloom = false;
        SDL_Log("[bench] bloom disabled");
    }
    if (const char* v = SDL_getenv("BENCH_NO_SSR"); v && *v && *v != '0') {
        renderer.toggles.ssr = false;
        SDL_Log("[bench] SSR disabled");
    }
    if (const char* v = SDL_getenv("BENCH_NO_GTAO"); v && *v && *v != '0') {
        renderer.toggles.ssao = false;
        SDL_Log("[bench] GTAO disabled");
    }
    if (const char* v = SDL_getenv("BENCH_NO_VOL"); v && *v && *v != '0') {
        renderer.toggles.volumetrics = false;
        SDL_Log("[bench] volumetrics disabled");
    }

    SDL_Log("[client] local player spawned at (0, 200, 0), physicsHz=%d", k_physicsHz);
    return true;
}

SDL_AppResult Game::event(SDL_Event* event)
{
    // Forward every event to ImGui first so it can capture keyboard/mouse
    // when the cursor is hovering over a window.
    debugUI.processEvent(event);
    hud_.processEvent(event);

    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    // Resize HUD offscreen target when the window pixel size changes.
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        const auto newW = static_cast<uint32_t>(event->window.data1);
        const auto newH = static_cast<uint32_t>(event->window.data2);
        hud_.resize(newW, newH);
        renderer.setHudTexture(hud_.getOutputTexture());
    }

    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.key) {
        case SDLK_MINUS:
            return SDL_APP_SUCCESS;

        // ESC — toggle mouse capture so the player can reach the ImGui windows.
        case SDLK_ESCAPE:
            mouseCaptured = !mouseCaptured;
            SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
            break;

            // F1 — send a test hello packet to the server.
            // case SDLK_F1: {
            //     static constexpr char k_helloMsg[] = "Hello from client!";
            //     client.send(k_helloMsg, static_cast<int>(sizeof(k_helloMsg) - 1));
            //     SDL_Log("Sent test packet to server");
            //     break;
            // }

        // F2 — toggle the unified debug menu. Individual panels are toggled
        // from checkboxes inside the menu, not all-at-once.
        case SDLK_F2:
            debugUI.toggleDebugMenu();
            // Auto-release cursor when opening the debug menu so the user can
            // interact with ImGui widgets immediately.
            if (debugUI.showDebugMenu && mouseCaptured) {
                mouseCaptured = false;
                SDL_SetWindowRelativeMouseMode(window, false);
            }
            break;

        // F3 — toggle mouse capture on/off independently.
        case SDLK_F3:
            mouseCaptured = !mouseCaptured;
            SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
            break;

        // Particle system test keys
        case SDLK_T: {
            // Energy beam — hits floor or max range
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;
            float dist = 500.f;
            glm::vec3 hitN = -cachedCamFwd_;
            if (cachedCamFwd_.y < -0.001f) {
                const float t = -cachedEye_.y / cachedCamFwd_.y;
                if (t > 0.f && t < dist) {
                    dist = t;
                    hitN = {0, 1, 0};
                }
            }
            const glm::vec3 hitP = cachedEye_ + cachedCamFwd_ * dist;
            particleSystem.spawnHitscanBeam(hip, hitP, WeaponType::EnergyGun);
            particleSystem.spawnImpactEffect(hitP, hitN, SurfaceType::Energy, WeaponType::EnergyGun);
            break;
        }
        case SDLK_Y: {
            // Bullet tracer — hits floor or max range
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;
            float dist = 500.f;
            glm::vec3 hitN = -cachedCamFwd_;
            if (cachedCamFwd_.y < -0.001f) {
                const float t = -cachedEye_.y / cachedCamFwd_.y;
                if (t > 0.f && t < dist) {
                    dist = t;
                    hitN = {0, 1, 0};
                }
            }
            const glm::vec3 hitP = cachedEye_ + cachedCamFwd_ * dist;
            particleSystem.spawnBulletTracer(hip, cachedCamFwd_, dist);
            particleSystem.spawnImpactEffect(hitP, hitN, SurfaceType::Metal, WeaponType::Rifle);
            break;
        }
        case SDLK_U: {
            particleSystem.spawnSmoke(cachedEye_ + cachedCamFwd_ * 200.f, 40.f);
            break;
        }
        case SDLK_I: {
            particleSystem.spawnExplosion(cachedEye_ + cachedCamFwd_ * 300.f, 100.f);
            break;
        }
        case SDLK_O: {
            particleSystem.drawScreenText({10.f, 40.f}, "HP 100  AMMO 30", {1.f, 1.f, 1.f, 1.f}, 24.f);
            break;
        }

        default:
            break;
        }
    }

    // NOTE: Local weapon VFX (tracers, impact, recoil) are handled continuously
    // in iterate() so held fire (auto weapons) spawns effects every cooldown tick.

    // Forward audio-device hot-swap events to the SFX system so it can
    // gracefully reopen when headphones are plugged / unplugged.
    if (event->type == SDL_EVENT_AUDIO_DEVICE_ADDED || event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED ||
        event->type == SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED)
    {
        sfxSystem.handleEvent(*event);
    }

    // Scroll wheel toggles between primary and secondary weapon slots.
    if (event->type == SDL_EVENT_MOUSE_WHEEL && mouseCaptured) {
        if (event->wheel.y > 0)
            pendingScrollSwitch_ = -1;
        else if (event->wheel.y < 0)
            pendingScrollSwitch_ = 1;
    }

    // Re-capture mouse on window click while uncaptured (standard FPS behaviour).
    // Suppress recapture when ImGui wants the mouse (hovering over a debug
    // window) or when the debug menu is open — let the user interact freely.
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse && !debugUI.showDebugMenu) {
            mouseCaptured = true;
            SDL_SetWindowRelativeMouseMode(window, true);
        }
    }

    return SDL_APP_CONTINUE;
}

/// @brief Advance one frame: decoupled physics / render loop.
///
/// Physics ALWAYS runs at exactly 128 Hz (k_physicsHz) using an accumulator
/// with a multi-tick catch-up loop (up to k_maxTicksPerFrame per call).
/// This is non-negotiable: it must match the server tick rate.
///
/// Input is split into two independent streams:
///
///   Mouse look (yaw / pitch) -- sampled EVERY iterate() call so camera
///       rotation is perfectly smooth at whatever frame rate the renderer
///       produces.  The camera always uses the latest yaw directly (never
///       interpolated).  Interpolating yaw with the physics alpha creates a
///       timebase mismatch on multi-tick or zero-tick frames, producing
///       visible jitter.
///
///   Movement keys (WASD / jump / crouch) -- sampled once per physics tick
///       group when inputSyncedWithPhysics is true (the default) so
///       movement calculations match the server.  When the toggle is off,
///       keys are also sampled every iterate() call.
///
/// Position interpolation uses alpha = accumulator / k_physicsDt across the
/// LAST physics tick (PreviousPosition is saved inside the while loop before
/// each tick).
///
/// Three ImGui-tunable flags:
///
///   renderSeparateFromPhysics -- render every iterate() call with position
///       interpolated between the last two physics ticks (true, default) vs.
///       render only after a physics tick (false, caps render fps at 128 Hz).
///
///   inputSyncedWithPhysics -- sample movement keys once per tick group
///       (true, default, server-consistent) vs. every iterate() call (false).
///       Mouse look is always per-frame regardless of this toggle.
///
///   limitFPSToMonitor -- when ON and monitor >= physicsHz, uses VSync.
///       When monitor < physicsHz (regardless of this toggle), a software
///       frame limiter at physicsHz is always active to ensure rock-steady
///       frame pacing — the monitor can't display above its refresh rate
///       anyway, and uncapped rendering creates beat-frequency jitter.

void Game::applyFrameRateLimit()
{
    // Query the monitor's native refresh rate.
    int monitorHz = 60; // safe fallback
    const SDL_DisplayID displayID = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayID);
    if (mode && mode->refresh_rate > 0.0f)
        monitorHz = static_cast<int>(std::ceil(mode->refresh_rate));

    if (limitFPSToMonitor && monitorHz >= k_physicsHz) {
        // Monitor is fast enough — VSync locks to monitor refresh without
        // starving the physics loop.
        renderer.setVSync(true);
        softLimitPeriod = 0;
    } else if (monitorHz < k_physicsHz) {
        // Monitor refresh is below physics Hz.  Regardless of the limiter
        // toggle, cap at physics Hz with mailbox presentation.  The monitor
        // can only display monitorHz frames per second anyway, and running
        // uncapped at extreme fps introduces frame-pacing variance that
        // creates visible beat-frequency jitter between the display refresh
        // and the physics tick rate.  The software limiter ensures rock-steady
        // frame spacing so the frames selected by mailbox presentation have
        // consistent interpolation coverage.
        renderer.setVSync(false);
        softLimitPeriod = SDL_GetPerformanceFrequency() / static_cast<Uint64>(k_physicsHz);
        softLimitNextFrame = SDL_GetPerformanceCounter() + softLimitPeriod;
        SDL_Log(
            "[client] monitor %d Hz < physics %d Hz — software limiter at %d fps", monitorHz, k_physicsHz, k_physicsHz);
    } else {
        // Monitor >= physics Hz, limiter off — truly uncapped.
        renderer.setVSync(false);
        softLimitPeriod = 0;
    }
}

SDL_AppResult Game::iterate()
{
    // 1. Accumulate real elapsed time
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_now = SDL_GetPerformanceCounter();

    float frameTime = static_cast<float>(k_now - prevTime) / static_cast<float>(k_perfFreq);

    // If the game was suspended (backgrounded / minimized), the raw delta can
    // be enormous.  Rather than trying to catch up through potentially seconds
    // of physics ticks (and replaying every buffered TCP message visually), we
    // detect the gap and skip straight to the present.  The next client.poll()
    // will still drain the TCP buffer so entity state snaps to current.
    static constexpr float k_suspendThreshold = 0.5f; // half-second gap = clearly suspended
    if (frameTime > k_suspendThreshold) {
        SDL_Log("[client] detected suspend (gap %.2fs) — resetting accumulator", static_cast<double>(frameTime));
        prevTime = k_now;
        accumulator = k_physicsDt; // run exactly one tick to apply latest state
        // Drain the network now so we snap to the current server state
        // without fast-forwarding through every intermediate update.
        client.poll(registry);
        refreshRemotePlayerRenderables();
        refreshRemoteProjectileRenderables();
        refreshRemoteRespawnRenderables();
        // Fall through to render the current frame normally.
    } else {
        frameTime = std::min(frameTime, 0.25f); // cap to avoid spiral-of-death
        prevTime = k_now;
        accumulator += frameTime;
    }

    static int iterCount = 0;
    if (false && ++iterCount <= 3)
        SDL_Log("[ITERATE] call=%d frameTime=%.4f acc=%.4f renderSep=%d",
                iterCount,
                static_cast<double>(frameTime),
                static_cast<double>(accumulator),
                renderSeparateFromPhysics);

    // 2. Refresh performance stats every 0.5 s
    static constexpr float k_statsPeriod = 0.5f;
    const float statsDt = static_cast<float>(k_now - statsPrevTime) / static_cast<float>(k_perfFreq);
    if (statsDt >= k_statsPeriod && fpsHistoryCount > 0) {
        // Physics rate: tick count / elapsed.
        measuredPhysicsHz = static_cast<float>(statsPhysTicks) / statsDt;
        statsPhysTicks = 0;
        statsPrevTime = k_now;

        // FPS percentile stats from the ring buffer.
        const int count = fpsHistoryCount; // may be < k_fpsHistorySize
        float sorted[k_fpsHistorySize];
        if (count < k_fpsHistorySize) {
            for (int i = 0; i < count; ++i)
                sorted[i] = fpsHistory[i];
        } else {
            // Full ring: oldest sample is at fpsHistoryHead.
            for (int i = 0; i < k_fpsHistorySize; ++i)
                sorted[i] = fpsHistory[(fpsHistoryHead + i) % k_fpsHistorySize];
        }
        std::sort(sorted, sorted + count); // ascending: worst fps first

        statsFPSMin = sorted[0];
        statsFPSMax = sorted[count - 1];
        statsFPS1pLow = sorted[static_cast<int>(static_cast<float>(count) * 0.01f)]; // 1st percentile
        statsFPS5pLow = sorted[static_cast<int>(static_cast<float>(count) * 0.05f)]; // 5th percentile
        // Most-recent sample (last written = head - 1).
        statsFPSCurrent = fpsHistory[(fpsHistoryHead - 1 + k_fpsHistorySize) % k_fpsHistorySize];
    }

    // Bench mode: collect per-frame timings (after warmup) then aggregate +
    // print + quit when the duration is up.
    if (benchActive_) {
        const float benchElapsed = static_cast<float>(k_now - benchStartTime_) / static_cast<float>(k_perfFreq);
        // Record this frame's wall-clock time once we're past the warmup window.
        if (benchElapsed >= k_benchWarmupSeconds && frameTime > 0.0f && frameTime < 0.25f)
            benchFrameTimesMs_.push_back(frameTime * 1000.0f);

        if (benchElapsed >= benchSeconds_) {
            const auto countSamples = benchFrameTimesMs_.size();
            if (countSamples == 0) {
                std::fprintf(stderr, "[bench] no samples collected (warmup window > duration?)\n");
                return SDL_APP_SUCCESS;
            }

            // Sort ascending: low ms = fast frames, high ms = slow frames.
            // We'll convert to FPS (=1000/ms) for the summary, so worst-frame
            // FPS = 1000 / max-ms = the LOW percentiles.
            auto frames = benchFrameTimesMs_;
            std::sort(frames.begin(), frames.end());
            auto pct = [&](float p) {
                const auto idx = static_cast<size_t>(static_cast<float>(countSamples - 1) * p);
                return frames[idx];
            };
            const float msMedian = pct(0.50f);
            const float msAvg = std::accumulate(frames.begin(), frames.end(), 0.0f) / static_cast<float>(countSamples);
            const float msP95 = pct(0.95f); // 95th-percentile slowest = 5% lows
            const float msP99 = pct(0.99f); // 99th-percentile slowest = 1% lows
            const float msFastest = frames.front();
            const float msSlowest = frames.back();

            auto fps = [](float ms) { return ms > 0.0f ? 1000.0f / ms : 0.0f; };

            std::fprintf(stderr,
                         "[bench] elapsed=%.1fs samples=%zu "
                         "avg=%.1f median=%.1f p5=%.1f p1=%.1f min=%.1f max=%.1f\n",
                         static_cast<double>(benchElapsed),
                         countSamples,
                         static_cast<double>(fps(msAvg)),
                         static_cast<double>(fps(msMedian)),
                         static_cast<double>(fps(msP95)),
                         static_cast<double>(fps(msP99)),
                         static_cast<double>(fps(msSlowest)),
                         static_cast<double>(fps(msFastest)));
            std::fflush(stderr);
            return SDL_APP_SUCCESS;
        }
    }

    // 3. Input
    //
    // Mouse look runs EVERY iterate() call — this keeps camera rotation
    // perfectly smooth at whatever frame rate the renderer is producing.
    // SDL_GetRelativeMouseState returns accumulated delta since last call,
    // so total rotation is identical regardless of call frequency.
    //
    // Movement keys run once per physics tick group (when inputSyncedWithPhysics
    // is true) so WASD movement calculations match the server.  When the
    // sync toggle is off, movement keys also run every frame.
    if (mouseCaptured) {
        systems::runMouseLook(registry, mouseSensitivity);
        if (!inputSyncedWithPhysics)
            systems::runMovementKeys(registry);
        systems::runWeaponKeys(registry);

        // Apply scroll-wheel weapon switch, constrained to primary/secondary.
        if (pendingScrollSwitch_ != 0) {
            registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) {
                int slotIdx = 0;
                registry.view<LocalPlayer, WeaponState>().each(
                    [&](const WeaponState& ws) { slotIdx = static_cast<int>(ws.current); });

                slotIdx = (slotIdx + pendingScrollSwitch_ + 2) % 2;
                snap.switchToPrimary = (slotIdx == 0);
                snap.switchToSecondary = (slotIdx == 1);
            });
            pendingScrollSwitch_ = 0;
        }
    }

    // Network stats: send periodic pings and update bandwidth counters
    client.updateStats(frameTime);
    pingTimer += frameTime;
    if (pingTimer >= 1.0f) {
        client.sendPing();
        pingTimer = 0.0f;
    }

    // 4. Physics -- always 128 Hz, up to k_maxTicksPerFrame catch-up
    bool physicsRan = false;
    int ticksThisFrame = 0;

    if (accumulator >= k_physicsDt) {
        // Movement keys: sample once for this whole group of ticks.
        if (inputSyncedWithPhysics && mouseCaptured)
            systems::runMovementKeys(registry);

        // Stamp the current InputSnapshot with the next predict tick BEFORE
        // sending. The server uses this tick for dedup against
        // lastAppliedInputTick, and Phase-5b prediction keys the input
        // ring buffer by it. We bump once per tick *group* (not per inner
        // tick) — multiple physics ticks in one frame share the same input,
        // because we only sample input once per group.
        ++clientPredictTick;
        registry.view<InputSnapshot, LocalPlayer>().each(
            [this](InputSnapshot& snap) { snap.tick = clientPredictTick; });

        // Send the redundant input batch (last k_inputRedundancy ticks).
        systems::runInputSend(registry, client);

        // Phase 5b: push the just-stamped input into the ring buffer so
        // reconciliation can replay it after a snapshot arrives.
        registry.view<LocalPlayer, InputSnapshot>().each(
            [this](const InputSnapshot& snap) { inputRing_.push(clientPredictTick, snap); });

        physicsRan = true;

        // Phase 5a: PreviousPosition is updated by Client::dispatchMessage
        // when a snapshot arrives, not every physics tick. The renderer
        // interpolates over the snapshot interval via
        // client.getSnapshotAlpha() so motion stays smooth at the much-
        // coarser snapshot rate (e.g. 32 Hz vs 128 Hz physics).
        //
        // Phase 5b: per physics tick we ALSO snapshot the local player's
        // pos→prev BEFORE running prediction so the renderer can show a
        // tick-rate-smooth interpolation of the local player even when
        // server snapshots arrive far less frequently. The local player
        // uses physics-tick alpha (in render code below); remote players
        // use the snapshot alpha.
        while (accumulator >= k_physicsDt && ticksThisFrame < k_maxTicksPerFrame) {
            accumulator -= k_physicsDt;

            // Phase 5b: capture local pos→prev for tick-rate interp,
            // then run client-side prediction. PlayerSimState filter on
            // runMovement narrows automatically to just the local player
            // (remotes don't have PlayerSimState on the client).
            registry.view<LocalPlayer, Position, PreviousPosition>().each(
                [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });
            systems::runPrediction(registry, k_physicsDt, physics::activeWorld());

            ++tickCount;
            ++ticksThisFrame;
            ++statsPhysTicks;
        }

        if (!client.poll(registry)) {
            // TODO: Update so reset to menu or some other non-crash state
            return SDL_APP_SUCCESS;
        }

        // Phase 5b: a snapshot just applied (overwriting the local
        // player's Position with the server's authoritative value at the
        // server-acked client tick). Replay the inputs we sent since
        // then to restore the predicted state at the *current* predict
        // tick — net effect is "server-side correction folded in,
        // client-side immediate response preserved".
        if (client.consumeSnapshotApplied()) {
            const uint32_t ackedTick = client.getServerAckedClientTick();
            if (ackedTick != 0 && clientPredictTick > ackedTick) {
                systems::runReconciliation(
                    registry, inputRing_, ackedTick, clientPredictTick, k_physicsDt, physics::activeWorld());
            }
        }

        refreshRemotePlayerRenderables();
        refreshRemoteProjectileRenderables();
        refreshRemoteRespawnRenderables();
    }

    // 5. Bail out early if there is nothing new to render
    if (!renderSeparateFromPhysics && !physicsRan)
        return SDL_APP_CONTINUE;

    // 6. Resolve camera
    glm::vec3 renderEye{0.0f, 100.0f, 0.0f};
    float renderYaw = 0.0f;
    float renderPitch = 0.0f;
    float targetRoll = 0.0f; // degrees, from PlayerVisState

    if (renderSeparateFromPhysics) {
        // Phase 5b: local player uses physics-tick alpha — its Position
        // updates every 7.8 ms (128 Hz) via client-side prediction, so
        // the lerp window matches that interval. Phase 5a's snapshot
        // alpha is used for *remote* entities (in the render-list loop
        // below) where Position only updates on snapshot arrival.
        const float alpha = std::clamp(accumulator / k_physicsDt, 0.0f, 1.0f);

        registry.view<LocalPlayer, Position, PreviousPosition, InputSnapshot, CollisionShape, PlayerVisState>().each(
            [&](const Position& pos,
                const PreviousPosition& prev,
                const InputSnapshot& input,
                const CollisionShape& shape,
                const PlayerVisState& pstate) {
                const glm::vec3 interpPos = glm::mix(prev.value, pos.value, alpha);
                const float eyeOffset = shape.halfExtents.y * 0.77f;
                renderEye = interpPos + glm::vec3{0.0f, eyeOffset, 0.0f};
                renderYaw = input.yaw;
                renderPitch = input.pitch;
                targetRoll = pstate.targetCameraTilt;
            });
    } else {
        // Sequential mode: use post-tick state directly (no interpolation).
        registry.view<LocalPlayer, Position, InputSnapshot, CollisionShape, PlayerVisState>().each(
            [&](const Position& pos,
                const InputSnapshot& input,
                const CollisionShape& shape,
                const PlayerVisState& pstate) {
                const float eyeOffset = shape.halfExtents.y * 0.77f;
                renderEye = pos.value + glm::vec3{0.0f, eyeOffset, 0.0f};
                renderYaw = input.yaw;
                renderPitch = input.pitch;
                targetRoll = pstate.targetCameraTilt;
            });
    }

    // Local weapon VFX — fires continuously while LMB is held, respecting cooldown.
    // This mirrors the server's fire rate so the local player sees tracers/impacts
    // at the same cadence as the server processes shots.
    // Beam weapons (EnergyGun) are driven by BeamState from the registry,
    // so they skip per-shot VFX here.
    {
        const WeaponConfig& wpnCfg = getWeaponConfig(currentEquippedType_);

        // Skip beam weapons (driven by BeamState) and charge weapons
        // (VFX arrive from server via NetParticleEvent on release).
        if (!wpnCfg.isBeam && !wpnCfg.isCharge) {
            localFireCooldown_ = std::max(0.0f, localFireCooldown_ - frameTime);

            const SDL_MouseButtonFlags mouseState = SDL_GetMouseState(nullptr, nullptr);
            const bool shooting = mouseCaptured && (mouseState & SDL_BUTTON_LMASK) != 0;

            // Check ammo — don't spawn VFX if the magazine is empty.
            bool hasAmmo = false;
            registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
                const GunInstance& gun = (ws.current == WeaponSlot::SECONDARY) ? ws.secondary : ws.primary;
                hasAmmo = gun.currentMagAmmo > 0 || gun.totalAmmo > 0;
            });

            if (shooting && localFireCooldown_ <= 0.0f && hasAmmo) {
                localFireCooldown_ = wpnCfg.fireCooldown;

                const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
                const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;

                // Raycast world geometry for tracer endpoint.  Impact effects
                // (sparks / blood / bullet holes) are NOT spawned here — they
                // come from the server's authoritative NetParticleEvent so that
                // player hits get the correct surface type and normal.
                const auto worldHit = physics::raycastWorld(cachedEye_, cachedCamFwd_, physics::activeWorld());
                const glm::vec3 hitPos = worldHit.hit ? worldHit.point : (cachedEye_ + cachedCamFwd_ * 5000.f);

                // Dispatch weapon-fired event for any listeners
                WeaponFiredEvent wfe;
                wfe.type = currentEquippedType_;
                wfe.origin = hip;
                wfe.direction = cachedCamFwd_;
                wfe.isHitscan = true;
                wfe.hitPos = hitPos;
                dispatcher.enqueue(wfe);

                // Spawn tracer from hip toward the crosshair hit point (not along
                // cachedCamFwd_ — the hip is offset from the eye, so the direction
                // to the hit point differs slightly from the camera forward).
                const glm::vec3 hipToHit = hitPos - hip;
                const float hipHitDist = glm::length(hipToHit);
                const glm::vec3 hipDir = (hipHitDist > 0.1f) ? hipToHit / hipHitDist : cachedCamFwd_;
                particleSystem.spawnBulletTracer(hip, hipDir, hipHitDist);

                // Visual recoil kick (viewmodel-only)
                const RecoilParams& rp = getRecoilParams(currentEquippedType_);
                recoilPitch_ += rp.pitchKick;
                recoilPushBack_ += rp.pushBack;
                recoilRoll_ += rp.rollKick * ((std::rand() % 2 == 0) ? 1.0f : -1.0f);
            }
        }
    }

    // Flush dispatcher events (weapon fired, impact, explosion)
    dispatcher.update();

    // Update particle system (render-rate, not physics-rate)
    particleSystem.update(frameTime, renderer.getCamera(), registry);

    // Update SFX system: retire finished voices, tick cooldowns, detect state changes.
    sfxSystem.update(frameTime, registry);

    // Weapon-specific sound state (charge rifle load, beam loop).
    if (sfxSystem.isInitialized()) {
        // Charge rifle: play load sound once when charging starts.
        bool isChargingNow = false;
        registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
            const GunInstance& gun = (ws.current == WeaponSlot::SECONDARY) ? ws.secondary : ws.primary;
            if (getWeaponConfig(gun.type).isCharge && gun.chargeTime > 0.0f)
                isChargingNow = true;
        });
        if (isChargingNow && !wasChargingRailgun_)
            sfxSystem.play(SfxId::ChargeRifleLoad);
        wasChargingRailgun_ = isChargingNow;

        // Energy beam: play/stop loop sound on beam active transitions.
        bool isBeamNow = false;
        registry.view<LocalPlayer, BeamState>().each([&](const BeamState& beam) { isBeamNow = beam.active; });
        if (isBeamNow && !wasBeamActive_)
            sfxSystem.play(SfxId::EnergyBeamLoop);
        if (!isBeamNow && wasBeamActive_)
            sfxSystem.stop(SfxId::EnergyBeamLoop);
        wasBeamActive_ = isBeamNow;

        // Beam hitmarker: client-side raycast against player hitboxes while firing.
        // Note: Player component is not synced to clients, so we raycast against
        // HitboxInstance directly (skipping the local player entity).
        if (isBeamNow) {
            registry.view<LocalPlayer, BeamState, InputSnapshot, Position, CollisionShape>().each(
                [&](entt::entity localE,
                    const BeamState&,
                    const InputSnapshot& inp,
                    const Position& pos,
                    const CollisionShape& shape) {
                    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f, 0.0f};
                    const float cp = std::cos(inp.pitch);
                    const glm::vec3 dir{std::sin(inp.yaw) * cp, -std::sin(inp.pitch), std::cos(inp.yaw) * cp};

                    physics::HitboxHit bestHit;
                    bestHit.distance = 5000.0f;
                    registry.view<Position, CollisionShape, HitboxInstance>().each([&](entt::entity target,
                                                                                       const Position& tPos,
                                                                                       const CollisionShape& tShape,
                                                                                       const HitboxInstance& hb) {
                        if (target == localE)
                            return;
                        const physics::WorldAABB bounds{tPos.value - tShape.halfExtents,
                                                        tPos.value + tShape.halfExtents};
                        float aabbDist = bestHit.distance;
                        glm::vec3 aabbN{0.0f};
                        if (!physics::raycastAABB(eye, dir, bounds, bestHit.distance, aabbDist, aabbN))
                            return;
                        for (const auto& cap : hb.capsules) {
                            float dist = bestHit.distance;
                            glm::vec3 n{0.0f};
                            if (!physics::raycastCapsule(
                                    eye, dir, cap.pointA, cap.pointB, cap.radius, bestHit.distance, dist, n))
                                continue;
                            bestHit.hit = true;
                            bestHit.distance = dist;
                            bestHit.point = eye + dir * dist;
                            bestHit.normal = n;
                            bestHit.region = cap.region;
                            bestHit.entity = target;
                        }
                    });

                    if (bestHit.hit) {
                        hitmarkerTimer_ = 0.10f; // short pulse — refreshed every frame while hitting
                        hitmarkerIsHeadshot_ = (bestHit.region == BodyRegion::Head);
                    }
                });
        }
    }

    // Draw persistent HUD text each frame
    // particleSystem.drawScreenText({10.f, 10.f}, "HP 100", {0.9f, 1.f, 0.9f, 1.f}, 22.f);

    // Speedometer HUD
    // Shows km/h with a horizontal bar that fills with speed.
    // 1 Quake unit ≈ 1 inch = 0.0254 m. Speed in u/s → km/h:
    //   km/h = (u/s) * 0.0254 * 3.6 = u/s * 0.09144
    {
        float playerSpeed = 0.0f;
        registry.view<LocalPlayer, Velocity>().each(
            [&](const Velocity& pvel) { playerSpeed = glm::length(pvel.value); });

        const float k_kmh = playerSpeed * 0.09144f;
        const float k_maxKmh = 120.0f; // bar fills fully at this speed

        // Speed number (bottom-right area of screen).
        char speedText[32];
        std::snprintf(speedText, sizeof(speedText), "%.0f km/h", static_cast<double>(k_kmh));
        // particleSystem.drawScreenText({10.f, 38.f}, speedText, {0.8f, 0.9f, 1.0f, 1.0f}, 18.f);

        // Speed bar: use block characters to draw a filled bar.
        const float k_fraction = std::clamp(k_kmh / k_maxKmh, 0.0f, 1.0f);
        const int k_barLen = static_cast<int>(k_fraction * 20.0f);

        // Color: green → yellow → red as speed increases.
        glm::vec4 barColor;
        if (k_fraction < 0.5f)
            barColor =
                glm::mix(glm::vec4(0.3f, 0.9f, 0.4f, 0.8f), glm::vec4(1.0f, 0.9f, 0.2f, 0.8f), k_fraction * 2.0f);
        else
            barColor = glm::mix(
                glm::vec4(1.0f, 0.9f, 0.2f, 0.8f), glm::vec4(1.0f, 0.25f, 0.15f, 0.9f), (k_fraction - 0.5f) * 2.0f);

        // Build bar string with block chars.
        char barStr[64] = {};
        for (int i = 0; i < k_barLen && i < 20; ++i)
            barStr[i] = '|';

        // Background (empty portion).
        char bgStr[64] = {};
        for (int i = 0; i < 20 - k_barLen && i < 20; ++i)
            bgStr[i] = '.';

        if (k_barLen > 0) {
            // particleSystem.drawScreenText({10.f, 58.f}, barStr, barColor, 16.f);
        }
        if (k_barLen < 20) {
            // particleSystem.drawScreenText(
            //     {10.f + static_cast<float>(k_barLen) * 8.f, 58.f}, bgStr, {0.3f, 0.3f, 0.3f, 0.4f}, 16.f);
        }
    }

    // Grapple cable visual
    registry.view<LocalPlayer, PlayerVisState>().each([&](const PlayerVisState& pstate) {
        if (pstate.grappleActive) {
            // Draw cable from player hand to hook point every frame.
            const float cosPi = std::cos(renderPitch);
            const glm::vec3 fwd{std::sin(renderYaw) * cosPi, -std::sin(renderPitch), std::cos(renderYaw) * cosPi};
            const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3{0, 1, 0}));
            const glm::vec3 hand = renderEye + right * 15.f - glm::vec3{0, 1, 0} * 8.f + fwd * 5.f;
            // particleSystem.spawnHitscanBeam(hand, pstate.grapplePoint, WeaponType::EnergyRifle);
        }
    });

    // Compute camera forward and cache for event() key shortcuts
    {
        const float cosPitch = std::cos(renderPitch);
        cachedCamFwd_ =
            glm::vec3{std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
        cachedEye_ = renderEye;
    }

    // Update skeletal animation + build the renderer's per-frame skinned
    // palette + instance arrays (perf Phase 1B).
    //
    // For each animated entity we:
    //   1. sample + blend its animation,
    //   2. populate JointMatrices for the hitbox system,
    //   3. append `numJoints` skin matrices to the flat palette,
    //   4. append one entry to the instance buffer (world transform +
    //      paletteBase = instanceIdx * numJoints).
    //
    // The renderer then issues one instanced draw per rig mesh per pass.  No
    // CPU skinning, no per-entity vertex re-upload, no per-entity draws.
    {
        std::vector<glm::mat4> bonePalette;
        std::vector<Renderer::SkinnedInstance> skinnedInstances;
        const int numJoints = charRig_.numJoints();
        if (numJoints > 0) {
            // Reserve a generous upper bound; the bench harness drives 100+
            // characters so this avoids constant reallocation at start-of-frame.
            bonePalette.reserve(static_cast<size_t>(numJoints) * 128);
            skinnedInstances.reserve(128);
        }
        const float snapshotAlpha = client.getSnapshotAlpha();

        // Phase 2 — frustum cull animated chars.  Build 6 view-frustum planes
        // from the camera's view-projection matrix; chars whose bounding sphere
        // lies fully outside any plane skip animation sampling AND skinned
        // draw entry.  Local player exempt (we still need its bones for hitbox /
        // weapon IK).  Bounding sphere is conservative: the rig's vertical
        // extent times an over-approximating 0.7 radius factor.
        const glm::mat4 vp = renderer.getCamera().getViewProjection();
        struct Plane
        {
            glm::vec3 n;
            float d;
        };
        Plane frustum[6];
        // Extract planes from row-form VP matrix (Gribb–Hartmann).  Each plane
        // points INWARD; a sphere is outside when distance < -radius.
        const glm::mat4 m = glm::transpose(vp);
        const auto extract = [&](int idx, const glm::vec4& row) {
            const glm::vec3 n(row.x, row.y, row.z);
            const float len = glm::length(n);
            frustum[idx].n = n / len;
            frustum[idx].d = row.w / len;
        };
        extract(0, m[3] + m[0]); // left
        extract(1, m[3] - m[0]); // right
        extract(2, m[3] + m[1]); // bottom
        extract(3, m[3] - m[1]); // top
        extract(4, m[3] + m[2]); // near
        extract(5, m[3] - m[2]); // far

        // Conservative sphere radius for an animated character — covers the
        // bind-pose AABB plus a fudge factor for arm/leg extension during run.
        const float charRadius = 1.5f * kRigScale_ * (rigMeshMinY_ < 0.0f ? -rigMeshMinY_ : 100.0f);

        auto inFrustum = [&](const glm::vec3& center, float radius) {
            for (const auto& p : frustum) {
                if (glm::dot(p.n, center) + p.d < -radius)
                    return false;
            }
            return true;
        };

        registry.view<AnimatedCharacter, Position, Velocity, PlayerVisState, InputSnapshot>().each(
            [&](entt::entity e,
                AnimatedCharacter& ac,
                const Position& pos,
                const Velocity& vel,
                const PlayerVisState& ps,
                const InputSnapshot& inp) {
                if (!ac.animator)
                    return;

                const bool isLocal = registry.all_of<LocalPlayer>(e);

                // Cull off-screen characters (except the local player; its
                // animator must keep running for hitboxes + weapon IK).
                const bool visible = isLocal || inFrustum(pos.value, charRadius);
                if (!visible)
                    return;

                // Phase 3c — animation tick decoupling.  ozz pose sampling +
                // skin-matrix flatten is the heaviest per-character CPU cost.
                // Cap the per-character animation update rate at ~30 Hz; in
                // between samples we reuse the previous skin matrices.  At
                // 1000 fps this skips ~32/33 frames of ozz work per char.
                constexpr float k_animationTick = 1.0f / 30.0f;
                ac.animationAccumulator += frameTime;
                if (ac.animationAccumulator >= k_animationTick || isLocal) {
                    ac.animationAccumulator = std::fmod(ac.animationAccumulator, k_animationTick);

                    AnimationInputs ai{};
                    ai.velocityWorld = vel.value;
                    ai.yawRad = inp.yaw;
                    ai.pitchRad = inp.pitch;
                    ai.grounded = ps.grounded;
                    ai.sprinting = ps.sprinting;
                    ai.crouching = ps.crouching;
                    ai.moveMode = static_cast<int>(ps.moveMode);
                    ai.wallRunSide = static_cast<int>(ps.wallRunSide);
                    ac.animator->update(ai, k_animationTick);

                    // Store model-space joint matrices for hitbox system.
                    auto& jm = registry.get_or_emplace<JointMatrices>(e);
                    jm.matrices = ac.animator->jointModelMatrices();
                }

                // Skip drawing the local player's own body in first-person
                // (the animator still ran above so hitboxes / future gun-IK
                // stay coherent).
                if (!animUI_.showLocalBody && isLocal)
                    return;

                if (numJoints == 0)
                    return;

                // Build per-instance world transform — same recipe as the
                // regular EntityRenderCmd path so the draw lines up exactly.
                glm::vec3 renderPos = pos.value;
                if (const auto* prev = registry.try_get<PreviousPosition>(e)) {
                    renderPos = glm::mix(prev->value, pos.value, snapshotAlpha);
                }
                glm::vec3 translation(0.0f);
                glm::vec3 scale(kRigScale_);
                glm::quat orient = glm::angleAxis(inp.yaw, glm::vec3{0, 1, 0});
                if (const auto* rend = registry.try_get<Renderable>(e)) {
                    translation = rend->translation;
                    scale = rend->scale;
                    orient = rend->orientation;
                } else {
                    // Fallback for entities that haven't reached the
                    // refreshRemotePlayerRenderables block yet this frame —
                    // align feet with AABB bottom from the rig metadata.
                    if (const auto* shape = registry.try_get<CollisionShape>(e))
                        translation = glm::vec3(0.0f, -shape->halfExtents.y - rigMeshMinY_ * kRigScale_, 0.0f);
                }

                glm::mat4 world = glm::translate(glm::mat4(1.0f), renderPos + translation);
                world *= glm::mat4_cast(orient);
                world = glm::scale(world, scale);

                Renderer::SkinnedInstance inst{};
                inst.worldTransform = world;
                inst.paletteBase = static_cast<uint32_t>(skinnedInstances.size() * static_cast<size_t>(numJoints));
                inst.materialId = 0;
                skinnedInstances.push_back(inst);

                const auto& sm = ac.animator->skinMatrices();
                bonePalette.insert(bonePalette.end(), sm.begin(), sm.end());
            });

        // Phase 3f — front-to-back sort by camera distance.  The bone palette
        // is keyed by paletteBase inside each SkinnedInstance, so we can
        // permute the instance vector independently of the palette and the
        // shader still finds each char's joints correctly.  Front-to-back
        // ordering means closer-to-camera chars draw first, write the depth,
        // and farther chars behind them get early-Z'd through the heavy
        // PBR fragment shader.  Trivial cost: 30·log(30) compares.
        if (!skinnedInstances.empty()) {
            const glm::vec3 camPos = cachedEye_;
            std::sort(skinnedInstances.begin(),
                      skinnedInstances.end(),
                      [&](const Renderer::SkinnedInstance& a, const Renderer::SkinnedInstance& b) {
                          const glm::vec3 ap = glm::vec3(a.worldTransform[3]);
                          const glm::vec3 bp = glm::vec3(b.worldTransform[3]);
                          return glm::dot(ap - camPos, ap - camPos) < glm::dot(bp - camPos, bp - camPos);
                      });
        }

        legacyRenderer().setSkinnedFrame(bonePalette, skinnedInstances);

        // Update hitbox capsules from bone transforms (client-side for debug visualization).
        if (charRig_.isLoaded())
            systems::updateHitboxes(registry, clientHitboxRig_, kRigScale_, rigMeshMinY_);
    }

    // Build entity render list
    {
        // Phase 5a: snapshot-rate alpha for remote-entity position lerp.
        // Read once per frame; applied to every entity below that has a
        // PreviousPosition (i.e. has received at least one snapshot).
        const float snapshotAlpha = client.getSnapshotAlpha();

        // Phase 3f: extract view frustum for culling entity render commands +
        // third-person weapons.  Same Gribb-Hartmann decomposition we use for
        // skinned chars, but reusable across both passes here.  Bounding-
        // sphere check per entity is ~24 ops — negligible — but cuts the
        // per-frame draw count by typically 50–80% on a 100-bot scene.
        const glm::mat4 cullVP = renderer.getCamera().getViewProjection();
        struct CullPlane
        {
            glm::vec3 n;
            float d;
        };
        CullPlane cullPlanes[6];
        {
            const glm::mat4 m = glm::transpose(cullVP);
            const auto extract = [&](int idx, const glm::vec4& row) {
                const glm::vec3 n(row.x, row.y, row.z);
                const float len = glm::length(n);
                cullPlanes[idx].n = n / len;
                cullPlanes[idx].d = row.w / len;
            };
            extract(0, m[3] + m[0]);
            extract(1, m[3] - m[0]);
            extract(2, m[3] + m[1]);
            extract(3, m[3] - m[1]);
            extract(4, m[3] + m[2]);
            extract(5, m[3] - m[2]);
        }
        const auto entityVisible = [&](const glm::vec3& center, float radius) {
            for (const auto& p : cullPlanes)
                if (glm::dot(p.n, center) + p.d < -radius)
                    return false;
            return true;
        };

        std::vector<EntityRenderCmd> entityCmds;
        registry.view<Position, Renderable>().each([&](entt::entity e, const Position& pos, const Renderable& rend) {
            if (!rend.visible || rend.modelIndex < 0)
                return;
            // Skip local player model in first-person (Option A from the plan).
            // The animator still runs so future gun-IK has an up-to-date pose;
            // only rendering is suppressed — and optionally re-enabled via the
            // "Show local body" debug toggle for third-person inspection.
            if (!animUI_.showLocalBody && registry.all_of<LocalPlayer>(e))
                return;

            // Phase 5a: lerp from PreviousPosition (= last snapshot) to
            // Position (= current snapshot) over the snapshot interval.
            // Entities without PreviousPosition (e.g. just-spawned) fall
            // back to the raw current position, but Client::dispatchMessage
            // seeds PreviousPosition on first snapshot apply so this
            // fallback should rarely fire in practice.
            glm::vec3 renderPos = pos.value;
            if (const auto* prev = registry.try_get<PreviousPosition>(e)) {
                renderPos = glm::mix(prev->value, pos.value, snapshotAlpha);
            }

            // Frustum cull — generous radius covers any rend.scale up to ~5×
            // the rig height.  Glow spheres / map props pass this trivially
            // when in view.  Out-of-view entities skip the per-mesh draw loop.
            const float entityRadius = 80.0f * std::max({rend.scale.x, rend.scale.y, rend.scale.z, 1.0f});
            if (!entityVisible(renderPos, entityRadius))
                return;

            glm::mat4 world = glm::translate(glm::mat4(1.0f), renderPos + rend.translation);
            world *= glm::mat4_cast(rend.orientation);
            world = glm::scale(world, rend.scale);

            entityCmds.push_back(EntityRenderCmd{.modelIndex = rend.modelIndex, .worldTransform = world});
        });

        // Third-person weapons for remote players
        registry.view<Position, InputSnapshot, WeaponState, CollisionShape>().each([&](entt::entity e,
                                                                                       const Position& pos,
                                                                                       const InputSnapshot& input,
                                                                                       const WeaponState& ws,
                                                                                       const CollisionShape&) {
            if (registry.all_of<LocalPlayer>(e))
                return;
            if (registry.all_of<RespawnTimer>(e))
                return;
            // Phase 3f: a remote third-person weapon ~50 world-units long;
            // a 100-unit cull radius around the player position is a tight
            // fit and skips the entire weapon draw when off-screen.
            if (!entityVisible(pos.value, 100.0f))
                return;

            const GunInstance& gun = (ws.current == WeaponSlot::SECONDARY) ? ws.secondary : ws.primary;
            const int wpnIdx = weaponModelIndices_[static_cast<int>(gun.type)];
            if (wpnIdx < 0)
                return;

            const auto& tp = tpWeaponParams_[static_cast<int>(gun.type)];

            // Player orientation vectors (horizontal plane)
            const float yaw = input.yaw;
            const glm::vec3 pFwd{std::sin(yaw), 0.0f, std::cos(yaw)};
            const glm::vec3 pRight = glm::normalize(glm::cross(pFwd, glm::vec3{0, 1, 0}));

            // Weapon world position = player center + hand offset
            glm::vec3 wpnPos =
                pos.value + pRight * tp.handOffset.x + glm::vec3{0, 1, 0} * tp.handOffset.y + pFwd * tp.handOffset.z;

            // Build transform: translate -> yaw -> pitch -> roll -> scale
            glm::mat4 wpnWorld = glm::translate(glm::mat4(1.0f), wpnPos);
            wpnWorld *= glm::rotate(glm::mat4(1.0f), yaw + glm::radians(tp.yawOffset), glm::vec3{0, 1, 0});
            float clampedPitch = std::clamp(input.pitch, glm::radians(-30.0f), glm::radians(30.0f));
            wpnWorld *= glm::rotate(glm::mat4(1.0f), clampedPitch + glm::radians(tp.pitchOffset), glm::vec3{1, 0, 0});
            wpnWorld *= glm::rotate(glm::mat4(1.0f), glm::radians(tp.rollOffset), glm::vec3{0, 0, 1});
            wpnWorld = glm::scale(wpnWorld, glm::vec3(tp.scale));

            entityCmds.push_back(EntityRenderCmd{.modelIndex = wpnIdx, .worldTransform = wpnWorld});
        });

        // Glow sphere — always rendered at a fixed world position for bloom testing.
        constexpr glm::vec3 glowSpherePos{0.0f, 80.0f, 300.0f};
        if (glowSphereModelIdx_ >= 0) {
            entityCmds.push_back(EntityRenderCmd{
                .modelIndex = glowSphereModelIdx_,
                .worldTransform = glm::translate(glm::mat4(1.0f), glowSpherePos),
            });
        }

        // Movable glow sphere — follows the player's view direction.
        const glm::vec3 movableSpherePos = cachedEye_ + cachedCamFwd_ * sphereFollowDist_;
        if (movableSphereEnabled_ && movableSphereModelIdx_ >= 0) {
            entityCmds.push_back(EntityRenderCmd{
                .modelIndex = movableSphereModelIdx_,
                .worldTransform = glm::translate(glm::mat4(1.0f), movableSpherePos),
            });
        }

        // Glow beam cylinder — follows player position and view direction.
        // Offsets are (forward, up, right) relative to camera.
        const glm::vec3 camRight = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
        const glm::vec3 camUp = glm::normalize(glm::cross(camRight, cachedCamFwd_));
        const glm::vec3 beamWorldStart =
            cachedEye_ + cachedCamFwd_ * beamStartOff_.x + camUp * beamStartOff_.y + camRight * beamStartOff_.z;
        const glm::vec3 beamWorldEnd =
            cachedEye_ + cachedCamFwd_ * beamEndOff_.x + camUp * beamEndOff_.y + camRight * beamEndOff_.z;

        if (beamEnabled_ && glowCylinderModelIdx_ >= 0) {
            // Update visual emissive color to match the color picker (HDR scaled).
            const float emScale = 10.0f;
            renderer.setModelEmissive(glowCylinderModelIdx_, glm::vec4(beamColor_ * emScale, 0.0f));
            entityCmds.push_back(EntityRenderCmd{
                .modelIndex = glowCylinderModelIdx_,
                .worldTransform = cylinderTransform(beamWorldStart, beamWorldEnd, beamRadius_),
            });
        }

        // Weapon beam visuals — driven by BeamState synced from server registry.
        // Local player: client-side predicted raycast for zero-lag response.
        // Remote players: use the server-computed positions from BeamState.
        registry.view<BeamState>().each([&](entt::entity e, const BeamState& beam) {
            if (!beam.active || glowCylinderModelIdx_ < 0)
                return;

            glm::vec3 beamOrigin = beam.origin;
            glm::vec3 beamEnd = beam.hitPoint;

            if (registry.all_of<LocalPlayer>(e)) {
                // Client-side prediction: raycast with this frame's camera
                // direction so the beam tracks the crosshair with zero latency.
                const float cosPitch = std::cos(renderPitch);
                const glm::vec3 fwd{
                    std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
                const glm::vec3 rgt = glm::normalize(glm::cross(fwd, glm::vec3{0, 1, 0}));
                const glm::vec3 up = glm::normalize(glm::cross(rgt, fwd));

                // Muzzle position from viewmodel offset.
                beamOrigin = renderEye + fwd * vmForward + rgt * vmRight - up * vmDown;

                // Predicted endpoint: raycast from eye along current view.
                const auto predictedHit = physics::raycastWorld(renderEye, fwd, physics::activeWorld());
                beamEnd = predictedHit.hit ? predictedHit.point : (renderEye + fwd * 5000.0f);
            }

            // Green Zarya-style tint, HDR-scaled for bloom.
            renderer.setModelEmissive(glowCylinderModelIdx_, glm::vec4(glm::vec3(0.3f, 1.0f, 0.2f) * 10.0f, 0.0f));

            entityCmds.push_back(EntityRenderCmd{
                .modelIndex = glowCylinderModelIdx_,
                .worldTransform = cylinderTransform(beamOrigin, beamEnd, 2.0f),
            });
        });

        renderer.setEntityRenderList(std::move(entityCmds));

        // Build dynamic point lights list.
        std::vector<PointLight> dynLights;

        // Static glow sphere point light.
        dynLights.push_back(PointLight{
            .position = glowSpherePos,
            .color = glm::vec3(1.0f, 0.6f, 0.2f),
            .intensity = 5.0f,
            .range = 500.0f,
        });

        // Flashlight — point light near the camera.
        if (flashlightEnabled_) {
            dynLights.push_back(PointLight{
                .position = cachedEye_ + cachedCamFwd_ * flashlightOffset_,
                .color = glm::vec3(1.0f, 0.95f, 0.9f),
                .intensity = flashlightIntensity_,
                .range = flashlightRange_,
            });
        }

        // Movable glow sphere point light.
        if (movableSphereEnabled_) {
            dynLights.push_back(PointLight{
                .position = movableSpherePos,
                .color = glm::vec3(0.4f, 0.7f, 1.0f),
                .intensity = sphereIntensity_,
                .range = sphereRange_,
            });
        }

        // Beam point lights — evenly distributed along the beam length.
        if (beamEnabled_) {
            const glm::vec3 beamDelta = beamWorldEnd - beamWorldStart;
            const float beamLen = glm::length(beamDelta);
            const int numBeamLights = (beamLightSpacing_ > 1.0f && beamLen > 0.1f)
                                          ? std::max(2, static_cast<int>(beamLen / beamLightSpacing_) + 1)
                                          : 2;
            const glm::vec3 beamLightColor = beamColor_ * 1.5f;
            for (int i = 0; i < numBeamLights; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(numBeamLights - 1);
                dynLights.push_back(PointLight{
                    .position = beamWorldStart + beamDelta * t,
                    .color = beamLightColor,
                    .intensity = beamLightIntensity_,
                    .range = beamLightRange_,
                });
            }
        }

        // Weapon beam point lights — from BeamState, evenly distributed.
        // Local player uses predicted positions (same as the visual beam above).
        registry.view<BeamState>().each([&](entt::entity e, const BeamState& beam) {
            if (!beam.active)
                return;

            glm::vec3 lightStart = beam.origin;
            glm::vec3 lightEnd = beam.hitPoint;

            if (registry.all_of<LocalPlayer>(e)) {
                const float cosPitch = std::cos(renderPitch);
                const glm::vec3 fwd{
                    std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
                lightStart = renderEye;
                const auto predictedHit = physics::raycastWorld(renderEye, fwd, physics::activeWorld());
                lightEnd = predictedHit.hit ? predictedHit.point : (renderEye + fwd * 5000.0f);
            }

            const glm::vec3 delta = lightEnd - lightStart;
            const float len = glm::length(delta);
            if (len < 1.0f)
                return;
            const int numLights = std::max(2, static_cast<int>(len / 80.0f) + 1);
            const glm::vec3 lightColor{0.3f, 1.0f, 0.2f};
            for (int i = 0; i < numLights && dynLights.size() < 14; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(numLights - 1);
                dynLights.push_back(PointLight{
                    .position = lightStart + delta * t,
                    .color = lightColor,
                    .intensity = 3.0f,
                    .range = 200.0f,
                });
            }
        });

        renderer.setPointLights(std::move(dynLights));
    }

    // Determine equipped weapon type from WeaponState
    registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
        const GunInstance& gun = (ws.current == WeaponSlot::SECONDARY) ? ws.secondary : ws.primary;
        currentEquippedType_ = gun.type;
    });

    // Auto-apply per-weapon viewmodel defaults when weapon changes
    if (currentEquippedType_ != lastEquippedType_) {
        const auto& vp = getViewmodelParams(currentEquippedType_);
        vmScale = vp.scale;
        vmForward = vp.forward;
        vmRight = vp.right;
        vmDown = vp.down;
        vmYawOffset = vp.yawOffset;
        vmPitchOffset = vp.pitchOffset;
        vmRollOffset = vp.rollOffset;
        lastEquippedType_ = currentEquippedType_;
    }

    const int currentWeaponModelIdx = weaponModelIndices_[static_cast<int>(currentEquippedType_)];

    // Build weapon viewmodel
    {
        WeaponViewmodel vm;
        const auto localDeadView = registry.view<LocalPlayer, RespawnTimer>();
        if (currentWeaponModelIdx >= 0 && localDeadView.begin() == localDeadView.end()) {
            vm.modelIndex = currentWeaponModelIdx;
            vm.visible = true;

            const float cosPitch = std::cos(renderPitch);
            const glm::vec3 forward{
                std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0, 1, 0}));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));

            // --- Weapon sway (CoD-style barrel lead) ---
            {
                if (!swayInitialized_) {
                    prevSwayYaw_ = renderYaw;
                    prevSwayPitch_ = renderPitch;
                    swayInitialized_ = true;
                }

                float yawDelta = renderYaw - prevSwayYaw_;
                float pitchDelta = renderPitch - prevSwayPitch_;
                prevSwayYaw_ = renderYaw;
                prevSwayPitch_ = renderPitch;

                // Wrap yaw delta for -pi/+pi boundary
                if (yawDelta > glm::pi<float>())
                    yawDelta -= glm::two_pi<float>();
                if (yawDelta < -glm::pi<float>())
                    yawDelta += glm::two_pi<float>();

                if (frameTime > 0.0001f) {
                    float targetX = std::clamp(yawDelta / frameTime * 0.05f * swayAmplitudeYaw_,
                                               -swayAmplitudeYaw_ * 3.0f,
                                               swayAmplitudeYaw_ * 3.0f);
                    float targetY = std::clamp(pitchDelta / frameTime * 0.05f * swayAmplitudePitch_,
                                               -swayAmplitudePitch_ * 3.0f,
                                               swayAmplitudePitch_ * 3.0f);

                    float alpha = std::min(1.0f, swaySmoothing_ * frameTime * 60.0f);
                    swayOffsetX_ = glm::mix(swayOffsetX_, targetX, alpha);
                    swayOffsetY_ = glm::mix(swayOffsetY_, targetY, alpha);
                }
                float decay = std::exp(-swayDecayRate_ * frameTime);
                swayOffsetX_ *= decay;
                swayOffsetY_ *= decay;
            }

            // --- Recoil decay ---
            {
                const RecoilParams& rp = getRecoilParams(currentEquippedType_);
                float decay = std::exp(-rp.recoverySpeed * frameTime);
                recoilPitch_ *= decay;
                recoilPushBack_ *= decay;
                recoilRoll_ *= decay;

                // Kill tiny residuals
                if (std::abs(recoilPitch_) < 0.01f)
                    recoilPitch_ = 0.0f;
                if (std::abs(recoilPushBack_) < 0.01f)
                    recoilPushBack_ = 0.0f;
                if (std::abs(recoilRoll_) < 0.01f)
                    recoilRoll_ = 0.0f;
            }

            // --- Velocity-direction-dependent bobbing ---
            float bobPhase = 0.0f;
            float bobAmpFwd = 0.0f;
            float bobAmpStrafe = 0.0f;

            registry.view<LocalPlayer, Velocity>().each([&](const Velocity& vel) {
                glm::vec3 hVel{vel.value.x, 0.0f, vel.value.z};
                glm::vec3 hFwd{forward.x, 0.0f, forward.z};
                float hFwdLen = glm::length(hFwd);
                if (hFwdLen < 0.001f) {
                    hFwd = glm::vec3{std::sin(renderYaw), 0.0f, std::cos(renderYaw)};
                } else {
                    hFwd /= hFwdLen;
                }
                glm::vec3 hRight = glm::normalize(glm::cross(hFwd, glm::vec3{0, 1, 0}));

                float fwdSpeed = std::abs(glm::dot(hVel, hFwd));
                float strafeSpeed = std::abs(glm::dot(hVel, hRight));

                bobPhase = static_cast<float>(SDL_GetTicks()) * 0.008f;

                if (fwdSpeed > 10.0f)
                    bobAmpFwd = std::min(fwdSpeed / 800.0f, 1.5f);
                if (strafeSpeed > 10.0f)
                    bobAmpStrafe = std::min(strafeSpeed / 800.0f, 1.0f);
            });

            // Forward: classic vertical-dominant bob
            float bobX = std::sin(bobPhase) * bobAmpFwd * 0.3f;
            float bobY = std::sin(bobPhase * 2.0f) * bobAmpFwd * 0.5f;

            // Strafe: horizontal-dominant bob at slightly different frequency
            bobX += std::sin(bobPhase * 0.9f) * bobAmpStrafe * 0.8f;
            bobY += std::sin(bobPhase * 1.8f) * bobAmpStrafe * 0.2f;

            // Position the weapon in camera space, then convert to world.
            glm::vec3 weaponPos = renderEye + forward * vmForward + right * vmRight - up * vmDown;
            // Apply sway
            weaponPos += right * swayOffsetX_ + up * swayOffsetY_;
            // Apply bob
            weaponPos += right * bobX + up * bobY;
            // Apply recoil pushback
            weaponPos -= forward * recoilPushBack_;

            // Build world transform: translate -> local-rotate -> camera-orient -> scale.
            //
            // 1) Camera orientation: maps model axes into camera space.
            const glm::mat4 cameraOrient = glm::mat4(glm::vec4(right, 0.0f),
                                                     glm::vec4(up, 0.0f),
                                                     glm::vec4(forward, 0.0f),
                                                     glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

            // 2) Local rotation offsets (yaw/pitch/roll in degrees) with recoil.
            const glm::mat4 localRot =
                glm::rotate(glm::mat4(1.0f), glm::radians(vmYawOffset), glm::vec3(0, 1, 0)) *
                glm::rotate(glm::mat4(1.0f), glm::radians(vmPitchOffset + recoilPitch_), glm::vec3(1, 0, 0)) *
                glm::rotate(glm::mat4(1.0f), glm::radians(vmRollOffset + recoilRoll_), glm::vec3(0, 0, 1));

            glm::mat4 weaponWorld = glm::translate(glm::mat4(1.0f), weaponPos);
            weaponWorld *= cameraOrient;
            weaponWorld *= localRot;
            // Negate X to cancel the reflection in the camera orient matrix
            // (right, up, forward has det = -1).
            weaponWorld = glm::scale(weaponWorld, glm::vec3(-vmScale, vmScale, vmScale));

            vm.transform = weaponWorld;
        }
        renderer.setWeaponViewmodel(vm);
    }

    // 7. Frame recording (R key) -- anchored to physics ticks
    if (physicsRan && recorder.isRecording()) {
        FrameState state;
        state.frameNumber = frameCount;
        state.timestamp = static_cast<double>(SDL_GetTicks()) / 1000.0 - recorder.startTimeSecs();
        state.tickCount = tickCount;
        state.renderEye = renderEye;
        state.renderYaw = renderYaw;
        state.renderPitch = renderPitch;

        registry.view<LocalPlayer, Position, Velocity, InputSnapshot>().each(
            [&](const Position& pos, const Velocity& vel, const InputSnapshot& input) {
                state.physPos = pos.value;
                state.physVel = vel.value;
                state.yaw = input.yaw;
                state.pitch = input.pitch;
            });

        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        const float winWf = static_cast<float>(winW);
        const float winHf = static_cast<float>(winH);

        const float cosPitch = std::cos(renderPitch);
        const glm::vec3 fwd{std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
        const glm::mat4 view = glm::lookAt(renderEye, renderEye + fwd, glm::vec3{0, 1, 0});
        const glm::mat4 proj =
            glm::perspective(glm::radians(60.0f), (winHf > 0.0f) ? winWf / winHf : 1.0f, 5.0f, 15000.0f);
        const glm::mat4 vp = proj * view;

        const auto toScreen = [&](glm::vec3 p) -> glm::vec2 {
            const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
            if (clip.w <= 0.0f)
                return {-1.0f, -1.0f};
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return {(ndc.x * 0.5f + 0.5f) * winWf, (1.0f - (ndc.y * 0.5f + 0.5f)) * winHf};
        };
        state.cubeScreen = toScreen(glm::vec3{0.0f, 32.0f, 400.0f});
        state.modelScreen = toScreen(glm::vec3{200.0f, 0.0f, 400.0f});

        char capPath[512];
        std::snprintf(capPath,
                      sizeof(capPath),
                      "%s/frame_%06llu.png",
                      recorder.sessionDir().c_str(),
                      static_cast<unsigned long long>(frameCount));
        state.screenshotPath = capPath;
        renderer.requestScreenshot(capPath);

        recorder.recordFrame(state);
    }

    // 8. FPS sample -- record inter-render delta into ring buffer
    if (prevRenderTime != 0) {
        const float renderDt = static_cast<float>(k_now - prevRenderTime) / static_cast<float>(k_perfFreq);
        if (renderDt > 0.0f && renderDt < 1.0f) { // ignore startup / minimised outliers
            fpsHistory[fpsHistoryHead] = 1.0f / renderDt;
            fpsHistoryHead = (fpsHistoryHead + 1) % k_fpsHistorySize;
            if (fpsHistoryCount < k_fpsHistorySize)
                ++fpsHistoryCount;
        }
    }
    prevRenderTime = k_now;

    ++frameCount;

    // 9. VSync toggle -- apply when limitFPSToMonitor changes
    // buildUI may modify limitFPSToMonitor, so we snapshot it before and
    // call setVSync only when it actually flips (avoids per-frame API calls).
    const bool prevLimitFPS = limitFPSToMonitor;

    // 10. Render
    debugUI.newFrame();

    // Bench mode skips the heavy debug panels; ImGui still draws but the
    // per-frame Build*UI cost is the difference between a few thousand ALU
    // ops and ~hundred-microsecond ImGui buffer construction at the high
    // bench frame-rates.
    if (!benchActive_) {
        // Unified debug menu — one window with toggles for every debug panel.
        debugUI.buildDebugMenu({
            {"HUD Tweaker", &showHudDebug_},
            {"Viewmodel Tweaker", &showViewmodelUI},
            {"3P Weapon Tweaker", &showTPWeaponUI_},
            {"Dynamic Lighting", &showDynLightUI_},
            {"Animation Tester", &animUI_.show},
        });

        debugUI.buildUI(registry,
                        tickCount,
                        mouseSensitivity,
                        renderSeparateFromPhysics,
                        inputSyncedWithPhysics,
                        limitFPSToMonitor,
                        renderer.ssrMode,
                        measuredPhysicsHz,
                        statsFPSCurrent,
                        statsFPSMin,
                        statsFPSMax,
                        statsFPS1pLow,
                        statsFPS5pLow);
        debugUI.buildNetworkUI(client.getNetStats());
    }

    // Phase 6 testing: network simulator window (latency + packet loss).
    // Slider values flow from DebugUI → Client each frame; idempotent when
    // unchanged (Client clamps and atomically stores). Latency is split
    // half-and-half across outbound + inbound delay queues; loss is an
    // independent Bernoulli drop applied per-datagram in each direction.
    debugUI.buildNetworkSimUI();
    client.setSimulatedLatencyMs(debugUI.getSimulatedLatencyMs());
    client.setSimulatedLossPercent(debugUI.getSimulatedLossPercent());

    // Scoreboard — now handled by the HUD Scoreboard widget (Tab key detected there).

    // Process ammo refill request — pulse refillAmmo on InputSnapshot for
    // exactly one frame so the server handles it once then stops.
    {
        const bool wantRefill = debugUI.pendingAmmoRefill_;
        debugUI.pendingAmmoRefill_ = false;
        registry.view<LocalPlayer, InputSnapshot>().each(
            [wantRefill](InputSnapshot& snap) { snap.refillAmmo = wantRefill; });
    }
    debugUI.buildParticleUI(particleSystem, cachedEye_, cachedCamFwd_);
    buildAnimationTesterUI(animUI_, registry, kRigScale_, kRigVerticalOffset_);

    // Hitbox debug visualization — project capsules into screen space.
    {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        const float winWf = static_cast<float>(winW);
        const float winHf = static_cast<float>(winH);
        const glm::mat4 hbView = glm::lookAt(cachedEye_, cachedEye_ + cachedCamFwd_, glm::vec3{0, 1, 0});
        const glm::mat4 hbProj =
            glm::perspective(glm::radians(60.0f), (winHf > 0.0f) ? winWf / winHf : 1.0f, 5.0f, 15000.0f);
        const glm::mat4 hbVP = hbProj * hbView;
        debugUI.buildHitboxUI(registry, clientHitboxRig_, hbVP, winWf, winHf);
        debugUI.buildCollisionUI(physics::activeWorld(), hbVP, winWf, winHf);
    }
#ifdef USE_HYBRID_RENDERER
    debugUI.buildRenderTogglesUI(renderer.legacy());
    debugUI.buildLightingUI(renderer.legacy());
    debugUI.buildSkyboxUI(renderer.legacy());
#else
    debugUI.buildRenderTogglesUI(renderer);
    debugUI.buildLightingUI(renderer);
    debugUI.buildSkyboxUI(renderer);
#endif

    HudDebugPanel::build(hud_, &showHudDebug_);

    // Viewmodel Tweaker — live-adjust weapon position, rotation, scale.
    if (showViewmodelUI) {
        if (ImGui::Begin("Viewmodel Tweaker", &showViewmodelUI)) {
            ImGui::SeparatorText("Position (Quake units)");
            ImGui::DragFloat("Forward", &vmForward, 0.5f, -50.0f, 100.0f, "%.1f");
            ImGui::DragFloat("Right", &vmRight, 0.5f, -50.0f, 50.0f, "%.1f");
            ImGui::DragFloat("Down", &vmDown, 0.5f, -50.0f, 80.0f, "%.1f");

            ImGui::SeparatorText("Rotation (degrees)");
            ImGui::DragFloat("Yaw", &vmYawOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("Pitch", &vmPitchOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("Roll", &vmRollOffset, 1.0f, -180.0f, 180.0f, "%.1f");

            ImGui::SeparatorText("Scale");
            ImGui::DragFloat("Scale", &vmScale, 0.001f, 0.001f, 10.0f, "%.4f");

            ImGui::Separator();
            const char* weaponNames[] = {"Rifle (R-301)", "Rocket", "RailGun (Triple Take)", "EnergyGun (Wingman)"};
            ImGui::Text("Equipped: %s", weaponNames[static_cast<int>(currentEquippedType_)]);

            if (ImGui::Button("Load weapon defaults")) {
                const auto& vp = getViewmodelParams(currentEquippedType_);
                vmScale = vp.scale;
                vmForward = vp.forward;
                vmRight = vp.right;
                vmDown = vp.down;
                vmYawOffset = vp.yawOffset;
                vmPitchOffset = vp.pitchOffset;
                vmRollOffset = vp.rollOffset;
            }

            ImGui::SeparatorText("Sway");
            ImGui::DragFloat("Sway Yaw Amp", &swayAmplitudeYaw_, 0.1f, 0.0f, 20.0f);
            ImGui::DragFloat("Sway Pitch Amp", &swayAmplitudePitch_, 0.1f, 0.0f, 20.0f);
            ImGui::DragFloat("Sway Decay", &swayDecayRate_, 0.5f, 1.0f, 30.0f);
            ImGui::DragFloat("Sway Smooth", &swaySmoothing_, 0.01f, 0.01f, 1.0f);

            ImGui::SeparatorText("Recoil");
            ImGui::Text("Pitch: %.2f  PushBack: %.2f  Roll: %.2f",
                        static_cast<double>(recoilPitch_),
                        static_cast<double>(recoilPushBack_),
                        static_cast<double>(recoilRoll_));
        }
        ImGui::End();
    }

    // Kill feed — tick timers and drop expired entries.
    for (auto& e : killFeed)
        e.displayTimer -= frameTime;
    std::erase_if(killFeed, [](const KillFeedEvent& e) { return e.displayTimer <= 0.0f; });

    // Kill feed overlay — now handled by HUD KillFeed widget.

    // Death HUD — bottom bar shown while local player is dead.
    {
        entt::entity localPlayer = entt::null;
        registry.view<LocalPlayer>().each([&](entt::entity e) { localPlayer = e; });

        if (localPlayer != entt::null && registry.all_of<DeathInfo, RespawnTimer>(localPlayer)) {
            const auto& deathInfo = registry.get<DeathInfo>(localPlayer);
            const auto& respawnTimer = registry.get<RespawnTimer>(localPlayer);

            ClientId localClientId{-1};
            registry.view<LocalPlayer, ClientId>().each([&](const ClientId& cid) { localClientId = cid; });

            char killerBuf[32];
            const char* killerName;
            if (localClientId.value != -1 && deathInfo.killerId == localClientId)
                killerName = "yourself";
            else {
                std::snprintf(killerBuf, sizeof(killerBuf), "Player #%d", deathInfo.killerId.value);
                killerName = killerBuf;
            }

            char line1[64], line2[64];
            std::snprintf(line1, sizeof(line1), "Killed by: %s", killerName);
            std::snprintf(line2,
                          sizeof(line2),
                          "Their HP: %.0f  Armor: %.0f  |  Respawning in %.0fs",
                          static_cast<double>(deathInfo.killerHealth.health),
                          static_cast<double>(deathInfo.killerHealth.armor),
                          std::ceil(static_cast<double>(respawnTimer.timeRemaining)));

            int winW = 0, winH = 0;
            SDL_GetWindowSizeInPixels(window, &winW, &winH);

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImFont* font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize();

            static constexpr float k_padX = 12.0f;
            static constexpr float k_padY = 8.0f;
            static constexpr float k_marginB = 16.0f;
            static constexpr float k_lineGap = 4.0f;

            const float boxH = fs * 2.0f + k_lineGap + k_padY * 2.0f;
            const float boxW = static_cast<float>(winW) * 0.4f;
            const float x = (static_cast<float>(winW) - boxW) * 0.5f;
            const float y = static_cast<float>(winH) - boxH - k_marginB;

            const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f));
            const ImU32 fg = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            const ImU32 fg2 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.85f, 0.85f, 0.85f, 0.85f));

            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH), bg, 4.0f);
            dl->AddText(font, fs, ImVec2(x + k_padX, y + k_padY), fg, line1);
            dl->AddText(font, fs, ImVec2(x + k_padX, y + k_padY + fs + k_lineGap), fg2, line2);
        }
    }

    // Hitmarker — now handled by HUD HitMarkerWidget.
    // Timer still ticks for HUD integration (hitmarkerTimer_ feeds HudGameState::hitConfirms).
    if (hitmarkerTimer_ > 0.0f)
        hitmarkerTimer_ -= frameTime;

    // Third-person weapon tweaker — per-weapon tuning for remote player weapons.
    if (showTPWeaponUI_) {
        if (ImGui::Begin("3P Weapon Tweaker", &showTPWeaponUI_)) {
            const char* tpWeaponNames[] = {"Rifle (R-301)", "Rocket", "RailGun (Triple Take)", "EnergyGun (Wingman)"};
            ImGui::Combo("Weapon", &tpTuneWeaponIdx_, tpWeaponNames, 4);

            auto& tp = tpWeaponParams_[tpTuneWeaponIdx_];

            ImGui::SeparatorText("Hand Offset (right, up, forward)");
            ImGui::DragFloat("TP Right", &tp.handOffset.x, 0.5f, -50.0f, 50.0f, "%.1f");
            ImGui::DragFloat("TP Up", &tp.handOffset.y, 0.5f, -50.0f, 50.0f, "%.1f");
            ImGui::DragFloat("TP Forward", &tp.handOffset.z, 0.5f, -50.0f, 50.0f, "%.1f");

            ImGui::SeparatorText("Rotation (degrees)");
            ImGui::DragFloat("TP Yaw", &tp.yawOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("TP Pitch", &tp.pitchOffset, 1.0f, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat("TP Roll", &tp.rollOffset, 1.0f, -180.0f, 180.0f, "%.1f");

            ImGui::SeparatorText("Scale");
            ImGui::DragFloat("TP Scale", &tp.scale, 0.0001f, 0.0001f, 10.0f, "%.5f");

            ImGui::Separator();
            if (ImGui::Button("Reset to defaults")) {
                tp = getThirdPersonWeaponParams(static_cast<WeaponType>(tpTuneWeaponIdx_));
            }
            ImGui::SameLine();
            if (ImGui::Button("Save as new defaults")) {
                SDL_Log("[client] 3P weapon %d: scale=%.5f offset=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.1f roll=%.1f",
                        tpTuneWeaponIdx_,
                        static_cast<double>(tp.scale),
                        static_cast<double>(tp.handOffset.x),
                        static_cast<double>(tp.handOffset.y),
                        static_cast<double>(tp.handOffset.z),
                        static_cast<double>(tp.yawOffset),
                        static_cast<double>(tp.pitchOffset),
                        static_cast<double>(tp.rollOffset));
            }
        }
        ImGui::End();
    }

    // Dynamic Lighting debug panel.
    if (showDynLightUI_) {
        ImGui::SetNextWindowPos({10.f, 400.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({280.f, 320.f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Dynamic Lighting", &showDynLightUI_)) {
            ImGui::SeparatorText("Flashlight");
            ImGui::Checkbox("Enable Flashlight", &flashlightEnabled_);
            if (flashlightEnabled_) {
                ImGui::DragFloat("FL Intensity", &flashlightIntensity_, 0.1f, 0.1f, 30.0f, "%.1f");
                ImGui::DragFloat("FL Range", &flashlightRange_, 10.0f, 50.0f, 3000.0f, "%.0f");
                ImGui::DragFloat("FL Offset", &flashlightOffset_, 1.0f, 0.0f, 100.0f, "%.0f");
            }

            ImGui::SeparatorText("Movable Glow Sphere");
            ImGui::Checkbox("Enable Sphere", &movableSphereEnabled_);
            if (movableSphereEnabled_) {
                ImGui::DragFloat("Follow Dist", &sphereFollowDist_, 5.0f, 30.0f, 500.0f, "%.0f");
                ImGui::DragFloat("Sph Intensity", &sphereIntensity_, 0.1f, 0.1f, 30.0f, "%.1f");
                ImGui::DragFloat("Sph Range", &sphereRange_, 10.0f, 50.0f, 3000.0f, "%.0f");
            }

            ImGui::SeparatorText("Bloom Beam");
            ImGui::Checkbox("Enable Beam", &beamEnabled_);
            if (beamEnabled_) {
                ImGui::Text("Offsets: (fwd, up, right) from eye");
                ImGui::DragFloat3("Start Off", &beamStartOff_.x, 1.0f, -500.0f, 500.0f, "%.0f");
                ImGui::DragFloat3("End Off", &beamEndOff_.x, 1.0f, -500.0f, 500.0f, "%.0f");
                ImGui::DragFloat("Radius", &beamRadius_, 0.5f, 0.5f, 50.0f, "%.1f");
                ImGui::ColorEdit3("Beam Color", &beamColor_.x);
                ImGui::DragFloat("Beam Intensity", &beamLightIntensity_, 0.1f, 0.1f, 30.0f, "%.1f");
                ImGui::DragFloat("Beam Lt Range", &beamLightRange_, 10.0f, 50.0f, 3000.0f, "%.0f");
                ImGui::DragFloat("Light Spacing", &beamLightSpacing_, 5.0f, 10.0f, 200.0f, "%.0f");
            }
        }
        ImGui::End();
    }

    // Update and render HUD.
    if (hud_.getOutputTexture()) {
        HudGameState hudState{};

        // ── Local player health, armor, alive ──
        registry.view<LocalPlayer, Health>().each([&](const Health& hp) {
            hudState.health = static_cast<int>(hp.health);
            hudState.maxHealth = 100;
            hudState.armor = static_cast<int>(hp.armor);
            hudState.maxArmor = 100;
        });
        registry.view<LocalPlayer, PlayerVisState>().each(
            [&](const PlayerVisState& ps) { hudState.isAlive = !ps.isDead; });

        // ── Weapon / ammo ──
        registry.view<LocalPlayer, WeaponState>().each([&](const WeaponState& ws) {
            const GunInstance* gun = &ws.primary;
            if (ws.current == WeaponSlot::SECONDARY) {
                gun = &ws.secondary;
            }
            hudState.ammoClip = gun->currentMagAmmo;
            hudState.ammoReserve = gun->totalAmmo;
            hudState.weaponId = static_cast<int>(gun->type);
        });

        // ── Round timer / buy phase ──
        hudState.roundTimeRemaining = countdownTimer;
        hudState.isBuyPhase = (currentMatchPhase == MatchPhase::WARMUP || currentMatchPhase == MatchPhase::COUNTDOWN);

        // ── Kill feed: convert KillFeedEvent → HudKillFeedEntry ──
        ClientId localClientId{-1};
        registry.view<LocalPlayer, ClientId>().each([&](const ClientId& cid) { localClientId = cid; });

        thread_local std::vector<HudKillFeedEntry> hudKillEntries;
        hudKillEntries.clear();
        // Only pass each event once — sentToHud flag prevents duplicates.
        for (auto& evt : killFeed) {
            if (!evt.sentToHud) {
                evt.sentToHud = true;
                HudKillFeedEntry entry;
                if (localClientId.value != -1 && evt.killerId == localClientId)
                    entry.killerName = "You";
                else {
                    char buf[16];
                    SDL_snprintf(buf, sizeof(buf), "Player #%d", evt.killerId.value);
                    entry.killerName = buf;
                }
                if (localClientId.value != -1 && evt.victimId == localClientId)
                    entry.victimName = "You";
                else {
                    char buf[16];
                    SDL_snprintf(buf, sizeof(buf), "Player #%d", evt.victimId.value);
                    entry.victimName = buf;
                }
                hudKillEntries.push_back(entry);
            }
        }
        hudState.killFeedEvents = hudKillEntries;

        // ── Hit confirms: feed from hitmarkerTimer_ (set by onParticleEvent) ──
        thread_local std::vector<HudHitConfirm> hudHitConfirms;
        hudHitConfirms.clear();
        if (hitmarkerTimer_ > 0.2f) { // just triggered (timer starts at 0.25)
            hudHitConfirms.push_back({hitmarkerIsHeadshot_, false, hitmarkerShieldBreak_});
        }
        hudState.hitConfirms = hudHitConfirms;

        // ── Vignette: detect health/armor deltas for damage & shield break ──
        {
            float curHealth = 100.f, curArmor = 100.f;
            registry.view<LocalPlayer, Health>().each([&](const Health& hp) {
                curHealth = hp.health;
                curArmor = hp.armor;
            });

            const float healthLost = prevHealth_ - curHealth;
            const float armorLost = prevArmor_ - curArmor;
            const float totalLost = std::max(0.f, healthLost) + std::max(0.f, armorLost);

            hudState.armorBroke = (prevArmor_ > 0.f && curArmor <= 0.f);

            // Red vignette on damage — suppressed when the shield breaks so
            // the blue shield-break vignette plays alone.
            if (totalLost > 0.f && !hudState.armorBroke) {
                hudState.tookDamage = true;
                hudState.damageIntensity = std::clamp(totalLost / 100.f, 0.f, 1.f);
            }

            prevHealth_ = curHealth;
            prevArmor_ = curArmor;
        }

        // ── Scoreboard: all players ──
        thread_local std::vector<HudTeamMemberStatus> hudAllPlayers;
        hudAllPlayers.clear();
        registry.view<ClientId, Health, PlayerVisState>().each(
            [&](entt::entity ent, const ClientId& cid, const Health& hp, const PlayerVisState& ps) {
                HudTeamMemberStatus status;
                if (localClientId.value != -1 && cid == localClientId)
                    status.name = "You";
                else {
                    char buf[16];
                    SDL_snprintf(buf, sizeof(buf), "Player #%d", cid.value);
                    status.name = buf;
                }
                status.health = static_cast<int>(hp.health);
                status.isAlive = !ps.isDead;
                if (const auto* pms = registry.try_get<PlayerMatchStats>(ent)) {
                    status.kills = pms->kills;
                    status.deaths = pms->deaths;
                    // Per-player ping: the server stamps each client's
                    // self-reported RTT onto their PlayerMatchStats.rttMs
                    // every tick (see ServerGame::updateLagCompTargets),
                    // which then rides the existing snapshot stream to
                    // every connected client. Reading from this row's
                    // own component instead of `client.getNetStats()`
                    // means every scoreboard row shows the right
                    // player's ping — not the local viewer's ping
                    // duplicated across every line.
                    status.ping = static_cast<int>(pms->rttMs);
                }
                hudAllPlayers.push_back(status);
            });
        // For now all players go into allies (no team system exists).
        hudState.allies = hudAllPlayers;
        hudState.allyScore = 0;
        hudState.enemyScore = 0;

        // ── Minimap: local player + all other player positions ──
        registry.view<LocalPlayer, Position, InputSnapshot>().each(
            [&](const Position& pos, const InputSnapshot& input) {
                hudState.localPlayerX = pos.value.x;
                hudState.localPlayerZ = pos.value.z;
                hudState.localPlayerYaw = input.yaw;
            });

        thread_local std::vector<HudMinimapDot> hudMinimapDots;
        hudMinimapDots.clear();
        registry.view<ClientId, Position, PlayerVisState>().each(
            [&](const ClientId& cid, const Position& pos, const PlayerVisState& ps) {
                if (ps.isDead)
                    return;
                if (localClientId.value != -1 && cid == localClientId)
                    return; // Skip local player (always drawn at center).
                hudMinimapDots.push_back({pos.value.x, pos.value.z});
            });
        hudState.enemyDots = hudMinimapDots;

        static int minimapLogTimer = 0;
        if (++minimapLogTimer % 300 == 1) // Log every ~5 seconds at 60fps
            SDL_Log("Minimap: localPos=(%.0f,%.0f) enemyDots=%zu",
                    static_cast<double>(hudState.localPlayerX),
                    static_cast<double>(hudState.localPlayerZ),
                    hudMinimapDots.size());

        // ── Screen dimensions ──
        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        hudState.screenW = static_cast<float>(winW);
        hudState.screenH = static_cast<float>(winH);

        // ── Floating damage numbers ──
        thread_local std::vector<HudDamageNumber> hudDamageNumbers;
        hudDamageNumbers.clear();
        for (const auto& pdn : pendingDamageNumbers_) {
            hudDamageNumbers.push_back(
                {pdn.pos.x, pdn.pos.y, pdn.pos.z, static_cast<int>(pdn.damage + 0.5f), pdn.headshot, pdn.shielded});
        }
        pendingDamageNumbers_.clear();
        hudState.damageNumbers = hudDamageNumbers;

        // ── Damage accumulator ──
        accumResetTimer_ -= frameTime;
        if (accumResetTimer_ <= 0.f) {
            accumTarget_ = entt::null;
            accumTotal_ = 0;
        }
        hudState.damageAccum.total = accumTotal_;
        if (accumLastHitType_ == 2)
            hudState.damageAccum.color = HudColor(1.0f, 0.85f, 0.2f, 1.f); // gold (headshot)
        else if (accumLastHitType_ == 1)
            hudState.damageAccum.color = HudColor(0.3f, 0.6f, 1.0f, 1.f);  // blue (shield)
        else
            hudState.damageAccum.color = HudColor(1.f, 1.f, 1.f, 1.f);     // white (health)

        // ── View-projection matrix for world→screen projection ──
        hudState.viewProj = renderer.getCamera().getViewProjection();

        hud_.update(frameTime, hudState);
        hud_.render();
    }

    debugUI.render();

    // Smooth camera roll interpolation (degrees → radians).
    {
        const float k_targetRad = glm::radians(targetRoll);
        const float k_speed = 10.0f; // interpolation speed (higher = snappier)
        currentCameraRoll_ += (k_targetRad - currentCameraRoll_) * std::min(1.0f, k_speed * frameTime);
        // Kill tiny residual to avoid permanent micro-tilt.
        if (std::abs(currentCameraRoll_) < 0.001f && std::abs(k_targetRad) < 0.001f)
            currentCameraRoll_ = 0.0f;
    }
    renderer.drawFrame(renderEye, renderYaw, renderPitch, currentCameraRoll_);

    if (limitFPSToMonitor != prevLimitFPS)
        applyFrameRateLimit();

    // Software frame limiter: sleep + spin-wait when targeting above monitor refresh.
    if (softLimitPeriod != 0) {
        const Uint64 perfFreq = SDL_GetPerformanceFrequency();
        const Uint64 now = SDL_GetPerformanceCounter();
        if (now < softLimitNextFrame) {
            const Sint64 sleepMs = static_cast<Sint64>((softLimitNextFrame - now) * 1000 / perfFreq) - 1;
            if (sleepMs > 0)
                SDL_Delay(static_cast<Uint32>(sleepMs));
            // Spin-wait for remaining sub-millisecond precision.
            while (SDL_GetPerformanceCounter() < softLimitNextFrame) {
            }
        }
        softLimitNextFrame = SDL_GetPerformanceCounter() + softLimitPeriod;
    }

    return SDL_APP_CONTINUE;
}

void Game::quit()
{
    if (recorder.isRecording())
        recorder.stopRecording();
    sfxSystem.quit();
    particleSystem.quit();
    hud_.quit();
    renderer.quit();
    debugUI.shutdown();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}

void Game::refreshRemotePlayerRenderables()
{
    // Remote players use the shared Mixamo rig — no more Wraith placeholder.
    // Scale + Y offset are driven from the auto-calculated values (and tunable
    // via the Animation Tester panel).
    //
    // The vertical offset places the model's feet at the bottom of the
    // collision AABB: translation.y = -halfExtents.y - meshMinY * scale.
    // This ensures the model tracks the AABB bottom automatically when
    // crouching changes the half-height — no manual offset update needed.
    registry.view<Position, PlayerVisState, InputSnapshot, CollisionShape>().each([&](entt::entity e,
                                                                                      const Position&,
                                                                                      const PlayerVisState& state,
                                                                                      const InputSnapshot& input,
                                                                                      const CollisionShape& shape) {
        if (registry.all_of<LocalPlayer>(e))
            return;

        if (!registry.all_of<AnimatedCharacter>(e))
            attachAnimatedCharacter(e);

        const auto& ac = registry.get<AnimatedCharacter>(e);
        if (!ac.animator)
            return; // rig unavailable — leave entity un-rendered rather than crash.

        auto& rend = registry.get_or_emplace<Renderable>(e);
        // Perf Phase 1B: animated chars are drawn by the renderer's instanced
        // skinned-rig pipeline, NOT via the regular EntityRenderCmd path.
        // Setting modelIndex = -1 keeps Renderable's translation/scale/orientation
        // up-to-date (used to derive the per-instance world transform below)
        // while preventing the entity-render loop from drawing a duplicate.
        rend.modelIndex = -1;
        // Bottom-of-AABB reference: align model feet with the AABB bottom.
        rend.translation = glm::vec3(0.0f, -shape.halfExtents.y - rigMeshMinY_ * kRigScale_, 0.0f);
        rend.scale = glm::vec3(kRigScale_);
        // No importFix quaternion here: rig is loaded with
        // AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS=false which collapses the
        // FBX pre-rotation.  Add a rig-local fix here if the rig ends up
        // facing the wrong axis after a visual check.
        rend.orientation = glm::angleAxis(input.yaw, glm::vec3{0, 1, 0});
        rend.visible = !state.isDead;
    });
}

void Game::refreshRemoteProjectileRenderables()
{
    registry.view<Position, Projectile, Velocity, CollisionShape>().each(
        [&](entt::entity e, const Position&, const Projectile&, const Velocity&, const CollisionShape& shape) {
            auto& rend = registry.get_or_emplace<Renderable>(e, Renderable{});
            rend.modelIndex = 1;

            // rend.translation = glm::vec3(0.0f, -shape.halfExtents.y - rigMeshMinY_ * kRigScale_, 0.0f);
            rend.scale = glm::vec3(10);
            // rend.orientation = glm::angleAxis(input.yaw, glm::vec3{0, 1, 0});
            rend.visible = true;
        });
}

void Game::refreshRemoteRespawnRenderables()
{
    registry.view<Position, WeaponSpawner, CollisionShape>().each(
        [&](entt::entity e, const Position&, const WeaponSpawner& spawner, const CollisionShape&) {
            auto& rend = registry.get_or_emplace<Renderable>(e, Renderable{});
            const int weaponIndex = static_cast<int>(spawner.type);
            if (weaponIndex < 0 || weaponIndex >= static_cast<int>(kWeaponAssets.size()) ||
                weaponAssetIds_[weaponIndex] < 0)
            {
                rend.modelIndex = -1;
                rend.visible = false;
                return;
            }

            const int assetId = weaponAssetIds_[weaponIndex];
            const AssetEntry& asset = assets_.entry(assetId);

            rend.modelIndex = asset.modelIndex;
            rend.scale = asset.renderScale;

            // Weapon bob and rotate
            static constexpr float k_spawnerSpinRadiansPerSec = glm::radians(45.0f);
            static constexpr float k_spawnerBobAmplitude = 6.0f;
            static constexpr float k_spawnerBobHz = 0.6f;
            static constexpr float k_twoPi = 6.28318530718f;

            const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;

            rend.visible = spawner.hasWeapon;

            if (spawner.hasWeapon) {
                rend.orientation =
                    glm::angleAxis(t * k_spawnerSpinRadiansPerSec, glm::vec3{0.0f, 1.0f, 0.0f}) * assetRotation(asset);

                rend.translation =
                    asset.renderTranslation +
                    glm::vec3{0.0f, std::sin(t * k_twoPi * k_spawnerBobHz) * k_spawnerBobAmplitude, 0.0f};
            } else {
                rend.orientation = assetRotation(asset);
                rend.translation = asset.renderTranslation;
            }
        });
}

void Game::attachAnimatedCharacter(entt::entity e)
{
    if (registry.all_of<AnimatedCharacter>(e))
        return;

    AnimatedCharacter ac;
    ac.animator = std::make_unique<CharacterAnimator>(charRig_, animLibrary_);
    ac.animator->setSkinningBackend(&skinBackend_);

    // Perf Phase 1B: animated chars draw via the renderer's shared skinned-rig
    // path (one VB/IB/textures for all chars, GPU LBS in vertex shader, instanced
    // draw).  No per-entity model clone, no CPU skinning, no per-frame vertex
    // re-upload.  modelIndex stays -1 so the regular EntityRenderCmd path
    // skips animated chars (they're drawn by the skinned pipeline instead).
    ac.modelIndex = -1;

    registry.emplace<AnimatedCharacter>(e, std::move(ac));
}
