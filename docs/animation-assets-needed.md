# Animation Assets Needed

The code now supports smooth start, stop, and pivot overlays, but the highest-quality result depends on authored FBX clips. Missing files do not block startup: the loader logs warnings and `CharacterAnimator` uses reduced-weight fallbacks.

Place these under `assets/animations/`.

## Required For The New Movement Polish

| ClipId | Expected file | Notes |
|---|---|---|
| `StartForward` | `loco/start_forward.fbx` | From idle into forward movement |
| `StartBackward` | `loco/start_backward.fbx` | From idle into backward movement |
| `StartLeft` | `loco/start_left.fbx` | From idle into left strafe |
| `StartRight` | `loco/start_right.fbx` | From idle into right strafe |
| `StopForward` | `loco/stop_forward.fbx` | Forward movement into idle |
| `StopBackward` | `loco/stop_backward.fbx` | Backward movement into idle |
| `StopLeft` | `loco/stop_left.fbx` | Left strafe into idle |
| `StopRight` | `loco/stop_right.fbx` | Right strafe into idle |
| `PivotLeft` | `loco/pivot_left.fbx` | Hard direction change toward left |
| `PivotRight` | `loco/pivot_right.fbx` | Hard direction change toward right |
| `CrouchWalk` | `crouch/Walk Crouching Forward.fbx` | Straight crouch walk forward; currently missing |

Author/export these in-place on the same skeleton as the current Mixamo-style rig. The importer strips Hips X/Z root motion, so preserve natural vertical bob but avoid baked world translation.

## Strongly Recommended Next

Download or author 8-way locomotion loops:

| Motion | Why |
|---|---|
| run forward-left and forward-right | Removes the current forward loop plus strafe-layer compromise at diagonals |
| run back-left and back-right | Better retreat/strafe readability |
| walk forward-left and forward-right | Cleaner low-speed strafing |
| walk back-left and back-right | Cleaner low-speed retreat strafing |
| crouch forward-left and forward-right | Existing assets have some of this, but should be normalized to the same skeleton/export settings |
| crouch back-left and back-right | Same as above |

## Optional But Needed For CS2-Level Feel

- Foot contact markers or notifies for left/right foot plant.
- Stop variants by speed band: walk-stop and run-stop.
- Start variants by speed band: walk-start and run-start.
- 90-degree and 180-degree pivots for both left and right.
- Weapon-specific upper-body additive poses for sprint, crouch, wallrun, and landing.
- Landing recovery clips for light and heavy landings.

Those are not required for this code path to run, but they are the art data that lets the system look deliberate instead of merely smoothed.

