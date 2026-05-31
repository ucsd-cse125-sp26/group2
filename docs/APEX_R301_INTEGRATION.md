# Apex R-301 first-person viewmodel + Wraith — integration notes

Status as of this branch (`apex-r301-integration`, off `apex-import`).

## What is DONE (assets)

Extracted from Apex Legends paks via `rsx_nogui.exe` and converted in Blender (Cast → GLB, Y-up):

- **`assets/apex_r301.glb`** (~10 MB) — R-301 first-person *gun* viewmodel.
  - Meshes: gun body + two `r101_magazine` meshes + integral front sight (attachments stripped).
  - Skeleton: 94-bone Apex `ptpov_rspn101` rig (Apex bone names, e.g. `def_c_magazine`, `def_c_bolt`, `weapon_bone`, `def_l/r_*` arms+fingers).
  - Textures embedded: winter skin `_col`→baseColor, `_nml`→normal, `_ilm`→emissive.
  - **8 animation clips** (glTF animations, absolute/playable directly):
    `reload`, `reload_empty`, `draw`, `drawfirst`, `holster`, `raise`, `lower`, `inspect`.
    Verified: `reload` drives `def_c_magazine` + `def_c_bolt` (mag ejects, bolt racks).
- **`assets/apex_r301_arms.glb`** (~14 MB) — Wraith first-person *arms* (`pov_pilot_light_wraith`) on the
  matching arms skeleton, same 8 clips (so gun + arms stay in sync when driven by the same clip+time),
  arm/gear textures embedded.

### Apex animation facts (important)
- First-person clips split: `reload/draw/holster/raise/lower/inspect` are **absolute** (play directly).
  `idle`, `fire`, `ads_in/out` are **additive** layers over a reference pose (`ref_0`) — NOT yet baked.
  For v1: use a static "ready" pose for idle and a procedural recoil kick for fire, OR bake
  `ref + additive` in Blender later.
- Gun and arms are **two meshes on two (compatible, same-named) skeletons**, exactly as Apex renders
  first-person. Do NOT try to merge skeletons — drive both with the same clip name + time.

### Raw extraction (kept under `C:\Users\user99\Downloads\rsx_2.1.0\`)
- `bulk/` — all `common.rpak` models (cast) incl. `mdl/r301_lgnd_v25_winter_v` and `mdl/pilot_light_wraith`.
- `bulk_anims/` — all 48k sequence casts (full paths). ptpov clips under
  `bulk_anims/animseq[_derived]/weapons/rspn101/ptpov_rspn101/`.
- `staging/weapon_clips/` — the 12 ptpov clips used.
- Wraith body `bulk/mdl/pilot_light_wraith/` + 651 of her clips under
  `bulk_anims/animseq/humans/class/light/pilot_light_wraith/` (her body textures still need extraction).

## Engine pipeline facts (verified)

- Skinned rendering is real and wired: `NewRenderer` calls `skinnedRenderer_.init/setRig/setFrame/uploadFrame/draw/drawDepth`
  (`src/client/renderer-new/NewRenderer.cpp:150,351,437,619,1117,1122`).
- Pipeline shaders: `shaders-new/skinned_geometry.vert` (GPU palette skinning) + `shaders-new/debug.frag`
  (**untextured** — needs a textured variant for the final look).
- `CharacterRig::loadFromFBX(path)` builds an ozz skeleton from Assimp — **works for GLB too**.
- `AnimationLibrary::loadClipFromFBX` reads **one clip per file** (`scene->mAnimations[0]`); a weapon
  library must read multiple `mAnimations` from one GLB by index/name.
- `SkinnedRenderer::setRig()` can be called **once** (`SkinnedRenderer.cpp:92`) — single rig today.
  Needs N-rig support (or one instance per rig) for body + gun + arms + Wraith.
- Weapon render path today is **static** (`drawWeapon → drawModel`, ignores bones). Weapon registry:
  `src/ecs/AssetCatalog.hpp:60` `kWeaponAssets` (Rifle = `assault_rifle.glb`).
  Reload today is a timer (`WeaponSystem::handleReload`), no animation = the "weapon-down" behavior.

## Engine work REMAINING (file-by-file)

1. **Textured skinned fragment shader** — add `shaders-new/skinned_geometry_textured.frag` (sample
   baseColor + normal, basic dir-light like `geometry.frag`); bind material textures in the skinned draw.
   Update `SkinnedRenderer::createSkinningPipeline` to use it and bind per-mesh textures.
2. **Multi-rig SkinnedRenderer** — refactor `SkinnedRenderer` to hold a map/vector of rigs (each: meshes,
   numJoints, per-frame palette+instances), or instantiate one `SkinnedRenderer` per rig. Draw all.
3. **WeaponViewmodel subsystem** (new, mirrors CharacterRig/AnimationLibrary/CharacterAnimator):
   - `WeaponRig` (load gun + arms GLB rigs), `WeaponClip` set, `WeaponAnimator` state machine:
     states Idle/Draw/Fire/Reload/ReloadEmpty/Holster; one-shot clips clamp+fire completion, idle = static
     ready pose; play same clip+time on gun and arms rigs.
   - Drive from `WeaponSystem`/`WeaponState`: on fire → Fire (or recoil add); on reload start → Reload
     (length = clip duration; set `reloadTime` from clip); on equip → Draw; on switch away → Holster.
     Remove the weapon-down lowering.
   - Position as a viewmodel: transform = camera/view space offset (tune translation/scale/rotation;
     GLB is Y-up at Apex native scale — expect a uniform scale + placement in front of camera).
4. **Wraith character** — load `pilot_light_wraith` as a `CharacterRig` instance; her Apex skeleton lacks
   Mixamo bone names so the player's procedural IK degrades gracefully; feed an idle/locomotion clip.
   (Needs her body textures extracted; bind-pose render works without.)
5. **Build/tune** — use `build_group2_rwdi.bat` (MSVC + `cmake --build build\relwithdebinfo --target group2`).
   NOTE: the existing `build_group2.bat` targets `build\debug`, which is NOT configured — it's a no-op.
   The configured build dir is `build/relwithdebinfo` (where `group2.exe` lives). Iterate on
   viewmodel scale/orientation, reload timing, shader look.

## STATUS: builds green (branch apex-r301-integration, uncommitted)

Implemented + compiling (`group2.exe` links):
- R-301 routed as the Rifle (`AssetCatalog.hpp`, renderScale 1.0).
- `WeaponViewmodelAnim` (animation/) — loads gun rig + all 8 clips from `apex_r301.glb`, ozz SamplingJob/LocalToModel → bone palette.
- `NewRenderer::viewmodelSkinned_` — 2nd SkinnedRenderer instance; `setViewmodelRig`/`setViewmodelFrame`; uploaded + drawn each frame.
- Game: loads viewmodel at init; per-frame plays `reload` clip on `gun.isReloading` (timed to `reloadTime`); weapon-down hack suppressed for Rifle.

### RUN-AND-REPORT checklist (needs eyes on the game — I'm headless)
1. Does the R-301 appear as the first-person weapon? (scale via `AssetCatalog` renderScale + `vmScale`; ~1.0 guess)
2. Is it visible & solid, or inside-out/invisible? If invisible → backface cull from the viewmodel's −X scale; fix = set `cullMode = SDL_GPU_CULLMODE_NONE` in `SkinnedRenderer::createSkinningPipeline`.
3. Trigger a reload — does the gun play the reload motion (mag out/in, bolt)?
4. Orientation/position correct relative to camera?
Report screenshots/logs (`[client] R-301 viewmodel: N joints, installed=1`) and I iterate.

### Iteration 2 (after motion confirmed)
- Textured skinned frag (currently `debug.frag` → untextured). Add `skinned_geometry_textured.frag` sampling base/normal + bind material textures in the skinned draw.
- Add the arms (`apex_r301_arms.glb`) as a 2nd viewmodel rig driven by the same clip.
- Draw viewmodel on-top (own pass / depth clear) so it doesn't clip world geometry.
- Bake additive idle/fire; map fire to a recoil kick.

## Open items / caveats
- Additive idle/fire not baked (see above).
- 18 cuff/finger-helper bones on the arms aren't in the gun skeleton (negligible sleeve detail).
- Wraith body textures not yet extracted (her materials live in `common.rpak`).
