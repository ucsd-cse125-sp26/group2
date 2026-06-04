# Animation Retargeting

This repo has two separate Apex animation paths:

- First-person viewmodel clips should stay on their Apex gun/arms rigs. Those clips drive weapon, magazine, bolt, and hand bones that do not exist on `character_rigged_new.glb`.
- Third-person body clips must be baked onto `assets/animations/character_rigged_new.glb` before the runtime can use them.

## R-301 Upper-Body Batch

The current automated batch retargets the R-301 third-person upper-body idle and reload clips:

```sh
python3 tools/retarget_apex_to_mixamo.py \
  --config tools/retarget_configs/r301_upperbody.json \
  --repo-root /home/user/Documents/dev/group2 \
  --execute
```

Generated outputs:

- `assets/animations/apex_retargeted/r301_idle_upper.fbx`
- `assets/animations/apex_retargeted/r301_reload_upper.fbx`
- `assets/blender_sources/r301_upperbody_retarget_preview.blend`

Run `--dry-run` to validate the config and print the planned clips without opening Blender.

## Extending To More Apex Clips

Add another clip entry to `tools/retarget_configs/r301_upperbody.json`, or copy that config for another weapon. Keep the same `bone_map` while the source skeleton is Wraith/Apex body animation. The tool validates input paths, mapped bone names, and output paths before executing Blender.

The retargeter samples the Apex source frame-by-frame, computes parent-local pose deltas, strips translation/scale/shear, and bakes rotation-only keys onto the target Mixamo bones. This keeps the target rig's proportions intact and avoids importing Apex bone lengths into the player mesh.

## Caveats

This is a first automated pass. The outputs are suitable for runtime wiring and animation-tester review, but weapon-relative polish may still need per-clip offsets, masking, or IK after the clips are in the game. The current tool exports animation-only FBX files on the target skeleton; it does not wire clips into `AnimationLibrary` or the upper-body reload state machine yet.
