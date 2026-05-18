# Wwise-Level Sound Engine Research

Last researched: 2026-05-18.

This note summarizes Wwise's core runtime ideas and turns them into a practical target for `group2`, a C++23 SDL3 Quake-style FPS with an existing SDL3 SFX system. It is not a recommendation to clone all of Wwise. The useful goal is a Wwise-like runtime architecture: data-driven sound objects, gameplay events, spatial emitters/listeners, mix routing, and performance-aware voice management.

Primary sources used:

- Audiokinetic, [Integrating Audio in your Game](https://www.audiokinetic.com/en/public-library/2025.1.3_9039/?id=integrating_audio_in_game&source=WwiseFundamentalApproach)
- Audiokinetic, [Understanding Events](https://www.audiokinetic.com/en/public-library/2025.1.4_9062/?id=understanding_events&source=WwiseFundamentalApproach)
- Audiokinetic, [Understanding SoundBanks](https://www.audiokinetic.com/en/public-library/2025.1.4_9062/?id=understanding_soundbanks&source=WwiseFundamentalApproach)
- Audiokinetic, [Integration Details - Game Objects](https://www.audiokinetic.com/en/public-library/2025.1.3_9039/?id=soundengine_gameobj.html&source=SDK)
- Audiokinetic, [Integrating Listeners](https://www.audiokinetic.com/fr/public-library/2025.1.3_9039/?id=soundengine_listeners.html&source=SDK)
- Audiokinetic, [Understanding Real-Time Parameter Controls](https://www.audiokinetic.com/en/public-library/2024.1.9_8920/?id=concept_rtpc.html&source=SDK)
- Audiokinetic, [Wwise visual elements](https://www.audiokinetic.com/en/public-library/2024.1.8_8898/?id=understanding_visual_elements_in_wwise&source=Help)
- Audiokinetic, [Switch Containers](https://www.audiokinetic.com/en/public-library/2024.1.7_8863/?id=defining_contents_and_behavior_of_switch_containers&source=Help)
- Audiokinetic, [Integration Details - States](https://www.audiokinetic.com/en/public-library/2025.1.3_9039/?id=soundengine_states.html&source=SDK)
- Audiokinetic, [Environments and game-defined auxiliary sends](https://www.audiokinetic.com/en/public-library/2025.1.3_9037/?id=soundengine_environments.html&source=SDK)
- Audiokinetic, [Busses hierarchy](https://www.audiokinetic.com/library/2025.1.3_9037/?id=master_mixer_objects_master_mixer&source=Help)
- Audiokinetic, [Playback limit and priority](https://www.audiokinetic.com/en/public-library/2025.1.3_9037/?id=concept_advanced_settings.html&source=SDK)
- Audiokinetic, [Virtual voices](https://www.audiokinetic.com/en/public-library/2025.1.3_9037/?id=concept_virtualvoices.html&source=SDK)
- Audiokinetic, [Spatial Audio rooms and portals advanced topics](https://www.audiokinetic.com/en/public-library/2025.1.4_9062/?id=spatial_audio_roomsportals_advancedtopics.html&source=SDK)
- Audiokinetic, [Working with Triggers](https://www.audiokinetic.com/en/public-library/2025.1.3_9037/?id=working_with_triggers&source=Help)

## Current Local Baseline

`docs/sfx.md` describes the current engine. Since this research note was first
written, the branch added the core Wwise-lite runtime:

- SDL3 custom mixer with predecoded float clips and a bounded 64-source pool.
- Data-driven `assets/audio/audio_manifest.toml` for clips, busses, nodes, and events.
- Stable FNV-1a IDs for events, nodes, clips, busses, switches, states, RTPCs, and audio objects.
- Semantic event posting on audio objects, with runtime transforms and velocities.
- `sound`, `random`, `sequence`, `switch`, and `blend` nodes.
- Object switches, object RTPCs, global states, bus gain routing, priority offsets, clip/bus limits, hot reload, and runtime stats.
- Spatial attenuation, stereo pan, Doppler, raycast occlusion, low-pass, simple reverb taps, and virtualized inaudible loops.

Remaining middleware-scale pieces are visual authoring, advanced room/portal propagation, dialogue/music specialty systems, HDR audio, and HRTF/object output.

## Wwise's Core Work Principles

### 1. Authoring/runtime separation

Wwise separates what designers author from what programmers drive. Designers create audio structures, events, game sync mappings, busses, and banks in Wwise Authoring. Programmers register game objects/listeners, load banks, post events, and update runtime parameters. This matters because the game code says "weapon fired" while data decides whether that means a random rifle shot, a tail, a distant layer, a bus duck, and a cooldown.

For `group2`, the equivalent should be:

- Programmers expose stable IDs: events, game parameters, switches, states, categories, emitters.
- Audio data lives in TOML/JSON or a compact generated asset file.
- Gameplay code posts semantic events, not clip filenames.

### 2. Runtime object model

Wwise centers runtime work on game objects. A game object can have a 3D transform, switch values, RTPC values, and object-specific properties. Events are posted on game objects. Listeners are also game objects, with position and orientation, and emitters are rendered relative to listeners.

For `group2`:

- Local player camera should be the default listener.
- Remote players, projectiles, explosions, impacts, pickups, and beam loops should be emitters.
- UI and local feedback sounds can be non-spatial/global.

### 3. Event-driven audio

Wwise events are action lists. An event may play, stop, pause, set volume, bypass an effect, change state, or trigger other changes. Events are stable integration points: game code posts an event, then audio content can evolve without recompiling gameplay code.

For `group2`, this should replace most direct `SfxSystem::play(SfxId)` calls with:

```cpp
audio.post(EventId::Weapon_Fire, emitter);
audio.post(EventId::Explosion, emitter);
audio.post(EventId::Local_Player_Damage, AudioObject::Global);
```

### 4. Game syncs

Wwise uses game syncs to bind game state to audio behavior:

- **RTPCs/game parameters:** continuous values such as speed, health, charge amount, distance, or shield strength.
- **Switches:** per-object alternatives, such as surface material, weapon type, team, movement mode, or armor type.
- **States:** broad/global modes, such as menu, round phase, slow motion, victory/defeat, low health, underwater, or muted pause.
- **Triggers:** spontaneous music cues, often for stingers.

For `group2`, RTPCs and switches are more important than states at first. A fast arena shooter benefits immediately from charge amount, distance, surface material, weapon type, and listener-relative position.

### 5. Hierarchical audio structures

Wwise audio objects are arranged in a hierarchy. Parents can apply shared properties. Important primitives include:

- **Sound SFX / Sound Voice:** playable sound objects.
- **Actor-Mixer:** parent/group object that applies shared properties to children.
- **Random Container:** chooses children randomly.
- **Sequence Container:** plays children in order.
- **Switch Container:** chooses child content based on a switch/state/RTPC.
- **Blend Container:** plays/crossfades multiple children according to an RTPC.
- **Music Track / Segment / Playlist Container / Switch Container:** interactive music primitives.

For `group2`, the valuable first subset is `Sound`, `Random`, `Sequence`, `Switch`, and later `Blend`.

### 6. Banks and loading boundaries

Wwise packages project data and media into SoundBanks. Init banks carry project-level definitions; content banks are loaded at useful moments to control memory.

For `group2`, this does not need to be a binary bank format immediately. A practical version:

- `assets/audio/audio_manifest.toml`: events, sound objects, busses, parameters.
- `assets/audio/banks/common.toml`: weapons, UI, player feedback.
- `assets/audio/banks/map_voidfall.toml`: ambiences, map-specific effects.
- Build-time validation that referenced files and IDs exist.
- Runtime bank load/unload and hot reload in debug builds.

### 7. Mix graph and routing

Wwise routes sound through busses. Audio busses group categories such as SFX, music, UI, voice. Auxiliary busses apply environmental effects through sends. Bus properties can include volume, effects, ducking, HDR, and processing mode.

For `group2`, build this as:

```text
Master
  SFX
    Weapons
    Impacts
    Player
    World
  UI
  Music
  VoiceChat
Aux
  SmallRoomReverb
  HangarReverb
```

The first implementation only needs gain routing. Aux sends/reverb can follow once spatial sound exists.

### 8. Voice lifecycle and performance policy

Wwise manages many simultaneous sounds with playback limits, priorities, and virtual voices. Low-priority or inaudible sounds can be killed or virtualized. A virtual voice is still tracked logically, but its audio is not processed until it becomes relevant again.

For `group2`, this should become a replacement for "recycle the voice furthest into playback":

- Each voice has priority.
- Priority can be offset by distance.
- Each sound/category/bus can have limits.
- New sounds can steal lower-priority voices.
- One-shots can be killed; loops can become virtual and resume.

### 9. Spatial audio

Wwise spatial audio includes emitter/listener transforms, attenuation, orientation, cones, obstruction/occlusion, game-defined aux sends, rooms, portals, diffraction, transmission, and geometry-assisted propagation.

For `group2`, implement in layers:

1. Distance attenuation and stereo panning.
2. Emitter/listener transforms from ECS.
3. Per-sound attenuation curves.
4. Doppler if projectile/vehicle motion makes it worthwhile.
5. Game-side occlusion raycasts.
6. Reverb zones or simple room volumes.
7. Portals/rooms only if the map layout needs it.

### 10. Interactive music

Wwise interactive music has segments, tracks, playlists, switch containers, transition rules, and triggers/stingers. This is powerful, but probably not the first need for `group2` unless the game has round-phase music.

A small useful version:

- Music states: Menu, Warmup, RoundActive, Overtime, Victory, Defeat.
- Crossfade transitions.
- Optional stingers for round start, final kill, win/loss.

## Wwise Primitive Inventory

| Wwise primitive | What it does | `group2` equivalent |
|---|---|---|
| SoundBank | Loadable package of media/data | Manifest/bank files plus decoded/preconverted clips |
| Init bank | Global project metadata | Audio registry loaded at startup |
| Event | Gameplay-triggered action list | `post(EventId, AudioObjectId)` |
| Event action | Play/stop/pause/set value/etc. | Data-driven command enum |
| Game object | Runtime emitter/target with state | `AudioObject { id, transform, switches, rtpcs }` |
| Listener | Microphone/camera game object | Local camera/player listener |
| Sound SFX | Playable clip | Existing `SoundClip` plus metadata |
| Actor-Mixer | Shared parent properties | Audio node group |
| Random Container | Variation | Random node with weights/no-repeat |
| Sequence Container | Ordered variation | Sequence node with cursor |
| Switch Container | Select child by switch/state/RTPC | Switch node keyed by material/weapon/etc. |
| Blend Container | Layer/crossfade by RTPC | Blend node for charge, speed, beam intensity |
| RTPC/Game Parameter | Continuous runtime control | Float parameter with curve mapping |
| Switch Group/Switch | Per-object categorical state | Surface, weapon, movement mode, team |
| State Group/State | Global categorical state | menu/round/pause/low-health mix states |
| Trigger | Spontaneous music cue | Stinger event |
| Bus | Mix category/routing node | Master/SFX/UI/Music/Voice bus tree |
| Aux Bus | Environmental/effect send | Reverb/effect bus |
| Game-defined aux send | Runtime environment send amount | Per-emitter aux send weights |
| Attenuation ShareSet | Reusable distance curves | Attenuation curve asset |
| Effect ShareSet | Reusable effect config | Reverb/filter/effect preset |
| Conversion Settings | Platform asset conversion | Offline or startup conversion policy |
| Playback limit | Max instances | Per-sound/category/bus voice cap |
| Playback priority | Importance score | Voice steal score |
| Virtual voice | Tracked but not processed voice | Silent loop tracker/resume policy |
| Dialogue Event | Decision tree over switches/states | Optional announcer/callout selector |
| Music Segment | Syncable music chunk | Optional music clip with bars/beats |
| Music Playlist Container | Ordered/random music sequence | Optional playlist player |
| Music Switch Container | Music selected by state | Optional round-phase music |
| Rooms/Portals | Spatial propagation abstraction | Later map acoustic zones |
| Obstruction/Occlusion | Blocked/muffled path controls | Raycast-driven low-pass/gain |
| Profiler/monitor | Runtime inspection | Debug HUD + voice graph + capture log |

## Recommended Engine Architecture

```text
Gameplay/ECS
  -> AudioSystem::post(event, object)
  -> AudioSystem::setSwitch(object, group, value)
  -> AudioSystem::setRtpc(object, parameter, value)
  -> AudioSystem::setState(group, value)

AudioSystem update
  1. Sync listener and emitter transforms from ECS.
  2. Drain queued audio commands.
  3. Resolve event actions into audio nodes.
  4. Evaluate containers, switches, randomization, RTPC curves.
  5. Allocate/steal/virtualize voices according to priority and limits.
  6. Compute per-voice gain, pitch, pan, attenuation, and sends.
  7. Feed SDL audio streams or a custom mixer.
  8. Emit debug/profiler stats.

Data
  AudioManifest
    banks
    clips
    events
    nodes
    busses
    rtpcs
    switches
    states
    attenuation curves
```

The most important design decision: make `EventId`, `AudioObjectId`, `BusId`, `RtpcId`, `SwitchGroupId`, and `StateGroupId` stable integer IDs generated from names. Use strings in tools/debug; use IDs in hot paths.

## Prioritized Primitive Matrix

Scores: impact 1-5, complexity 1-5. "Priority" combines value, project fit, and dependency order.

| Primitive | Impact | Complexity | Priority | Why |
|---|---:|---:|---|---|
| Event API with action list | 5 | 2 | P0 | Implemented. |
| Data manifest for clips/events | 5 | 2 | P0 | Implemented as TOML. |
| Stable generated IDs | 4 | 2 | P0 | Implemented with FNV-1a name IDs. |
| Voice priority + stealing | 5 | 2 | P0 | Implemented in the source manager. |
| Real looping voices | 4 | 2 | P0 | Implemented with source handles and inaudible-loop virtualization. |
| 3D emitter/listener transforms | 5 | 3 | P0 | Implemented through audio objects and listener state. |
| Distance attenuation curves | 5 | 2 | P0 | Implemented as per-clip full/max distances; true curve assets can follow. |
| Stereo panning | 5 | 3 | P0 | Implemented. |
| Bus/category routing | 4 | 2 | P0 | Implemented as parented bus gains plus categories. |
| Random container | 4 | 2 | P1 | Implemented. |
| Switches + switch container | 5 | 3 | P1 | Implemented for object switches and global states. |
| RTPC values + curves | 5 | 3 | P1 | Implemented as blend nodes over RTPC values. |
| Per-sound/category/bus voice limits | 4 | 3 | P1 | Implemented for clip and bus limits; category-specific limits can map to busses. |
| Debug voice profiler panel | 4 | 2 | P1 | Runtime stats exist; UI panel still pending. |
| Stream/voice preallocation | 4 | 2 | P1 | Implemented by replacing per-shot streams with one mixer/source pool. |
| Attenuation ShareSets | 4 | 2 | P1 | Partially implemented as per-clip distances; reusable named ShareSets still pending. |
| Sequence container | 3 | 2 | P2 | Implemented. |
| States | 3 | 2 | P2 | Implemented for state-driven switch nodes. |
| Aux sends + simple reverb zones | 4 | 4 | P2 | Partially implemented as source reverb send/taps; authored zones and aux busses are pending. |
| Occlusion raycasts | 4 | 4 | P2 | Implemented as listener-to-source raycasts that drive gain/filter/reverb. |
| Music state machine | 3 | 3 | P2 | Good if round music exists; not core combat audio. |
| Blend container | 4 | 4 | P2 | Implemented for RTPC-driven clip choice/layering. |
| Hot reload for audio manifest | 3 | 3 | P2 | Implemented through `SfxSystem::reloadAudioManifest()`. |
| Dialogue event selector | 2 | 3 | P3 | Only needed for announcer/commentary complexity. |
| Virtual voices | 3 | 4 | P3 | Implemented for inaudible loops; richer resume policy can still evolve. |
| Doppler | 2 | 3 | P3 | Implemented with conservative clamps. |
| Rooms/portals | 2 | 5 | P3 | Wwise-grade acoustics are overkill unless maps demand them. |
| Geometry diffraction/transmission | 1 | 5 | P4 | Too expensive and subtle for current scope. |
| Full authoring editor | 2 | 5 | P4 | A TOML/JSON manifest and debug UI are enough for this project. |
| HDR audio | 2 | 5 | P4 | Sophisticated mix feature; priority/limits solve the immediate problem. |
| HRTF/object-based output | 2 | 5 | P4 | Platform-heavy; stereo panning gives most value now. |

## Suggested Implementation Roadmap

### Phase 0: Stabilize the Existing Mixer

- Preallocate voice streams.
- Add true loop mode.
- Add priority fields and deterministic voice stealing.
- Add debug counters: active voices, dropped voices, stolen voices, event counts.

Status: implemented. The current SFX system now mixes through one playback path
with a bounded source pool, loop handles, priority stealing, virtualization, and
runtime counters.

### Phase 1: Data-Driven Events

- Add `AudioManifest` with clips, events, categories, cooldowns, gains, loop flags, priorities.
- Replace direct hardcoded `SfxId` play sites with `EventId` posts.
- Keep current `SfxId` internally during migration if needed.

Status: implemented. Gameplay code now posts semantic audio events for weapons,
impacts, explosions, player feedback, beam loops, and footsteps.

### Phase 2: Spatial Core

- Add `AudioObjectId` and emitter registry.
- Add listener synced from camera/local player.
- Add distance attenuation and stereo pan.
- Add per-event/object `spatial = true/false`.
- Route local UI/player feedback as 2D; world combat as 3D.

Status: implemented. Explosions, shots, impacts, footsteps, voice, and loops can
be positioned on audio objects with listener-relative attenuation, pan, and
Doppler.

### Phase 3: Wwise-Lite Nodes

- Add audio nodes: `Sound`, `Random`, `Sequence`, `Switch`.
- Add switch groups: surface material, weapon type, team, movement mode.
- Add RTPC curves for charge amount, beam intensity, low health, and optional distance-derived controls.

Status: implemented. The runtime supports `sound`, `random`, `sequence`,
`switch`, and `blend` nodes with object switches, global states, and RTPCs.

### Phase 4: Mix Graph

- Add bus tree and bus gain evaluation.
- Add per-bus limits and priorities.
- Add UI/debug controls for bus volumes.
- Add simple ducking if voice chat or announcer needs it.

Status: implemented except dedicated in-game mixer UI. The bus tree, bus gain,
bus voice limits, priority offsets, and hot reload path exist.

### Phase 5: Environment

- Add simple reverb zones or map volumes.
- Add aux sends per emitter.
- Add optional occlusion raycast from listener to emitter.

Status: partially implemented. Raycast occlusion, low-pass, and simple reverb
taps exist; authored acoustic volumes, aux bus routing, rooms, and portals are
still future work.

### Phase 6: Music and Polish

- Add music state machine and crossfades.
- Add stingers/triggers for round events.
- Add hot reload and validation tooling.

Status: hot reload exists; interactive music, stingers, dialogue routing, and
standalone validation tooling are still pending.

## Recommended Minimal Data Shape

Example sketch:

```toml
[[clips]]
id = "rifle_fire_01"
file = "assets/sounds/rifle_fire_01.wav"
category = "Weapons"
gain = 0.75
cooldown = 0.08
priority = 70
loop = false

[[events]]
id = "weapon.rifle.fire"

[[events.actions]]
type = "play"
target = "node.rifle_fire"

[[nodes]]
id = "node.rifle_fire"
type = "random"
no_repeat = true
children = ["rifle_fire_01", "rifle_fire_02", "rifle_fire_03"]

[[attenuations]]
id = "weapon_medium"
max_distance = 2200.0
curve = [[0, 1.0], [400, 0.85], [1400, 0.35], [2200, 0.0]]

[[busses]]
id = "Weapons"
parent = "SFX"
volume = 1.0
max_voices = 16
```

## Practical "Do Not Build Yet" List

Avoid these until the core is working:

- Visual Wwise-like authoring editor.
- Binary bank compiler.
- Rooms/portals/diffraction.
- HDR audio.
- Object-based surround/HRTF.
- Full dialogue decision tree.
- Sophisticated interactive music transitions.

Each is real Wwise functionality, but each also risks eating the project. The FPS will sound dramatically better from event data, spatialization, variation, and voice policy alone.

## First Target Status

The original first milestone was:

```text
Data-driven event + priority voice manager + true loops + 3D attenuation/pan.
```

That milestone is now implemented on this branch. The useful next layer is not
more hardcoded playback; it is production content, an in-game audio debug/mixer
panel, authored acoustic zones, and optional specialty systems for music or
dialogue if the game design needs them.
