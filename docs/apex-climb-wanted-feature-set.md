# Apex-Style Climb Wanted Feature Set

Source: https://apexmovement.tech/wiki/tech/General%20Tech%3EWall%20Tech%3EClimb%20Fundamentals%3EClimb%20Fundamentals

This file translates Apex's climb fundamentals into a target feature set for our game. It is not a claim that every Apex number should be copied 1:1; units, player capsule size, wallrun rules, and map scale must be tuned against our movement model.

## Design Target

Climbing should feel like a surface attachment mode, not a canned upward elevator. The player should be able to:

- attach from airborne motion into a valid wall;
- briefly stick even without perfect forward input;
- climb upward when input/momentum points into the wall;
- slip or climb downward when input/momentum does not support upward climb;
- carry useful incoming speed into climb, wallbounce, wall push, end boost, and wallrun transitions;
- detach predictably by jump, crouch, back input, invalid look angle, invalid surface angle, timer expiry, or ledge/mantle transition;
- reattach in-air only when attach-point and climb-space rules allow it.

## Core Mechanics

### Attach Requirements

- Player must be airborne. Grounded attach should not occur directly; jumping or falling off a ledge makes the player eligible.
- Surface must be climbable: steep enough to behave like a wall, not a floor or shallow ramp.
- View direction can be offset from the wall normal up to a configurable angle. Apex uses an arccos(0.7)-derived limit around 45.57 degrees; our current 30 degree gate is too strict for Apex-like climbing.
- Movement toward the wall should help attach, but full forward input should not be mandatory. Momentum into the wall can create a brief stick.
- Initial attach records:
  - wall normal;
  - attach point on wall;
  - attach height along local up;
  - climb-space baseline/cutoff;
  - incoming velocity projected into climb tangent axes;
  - whether the attach came from wallrun, jump, fall, grapple, launch, or ordinary air movement.

### Attach Point

- The attach point is fixed for the lifetime of a single climb.
- It must not slide upward while the player slips or climbs. This is important because Apex reattach and climb limits depend on the original attach point, not the current body position.
- Detach and reattach rules compare the next potential attach against the previous attach point and previous wall normal.

### Climb Space

We should model climb space explicitly instead of using only `climbTimer`.

Target concepts:

- `baseline`: bottom of climb space, usually based on last full ground contact.
- `mini zone`: low zone above baseline; jumping here gives small height and low wall-push force.
- `green zone`: middle zone used for wallbounces; jumping lower in the zone gives more height.
- `neutral zone`: upper zone; jump becomes mostly horizontal wall push.
- `cutoff`: top of climb space; reaching it ends climb and can trigger end boost.

Initial implementation can use approximate configurable heights in our units:

- mini zone: baseline to baseline + `k_climbMiniZoneHeight`;
- green zone: next `k_climbGreenZoneHeight`;
- neutral zone: until `k_climbSpaceHeight`;
- cutoff: baseline + `k_climbSpaceHeight`.

### Upward Climbing

- Upward climbing is based on local-up velocity, not world +Y.
- If local-up velocity is above a small threshold, climbing should not consume a non-upward timer.
- Input/momentum into the wall should generate climb-up force.
- Incoming velocity parallel to the wall should be preserved as much as possible instead of being zeroed.
- Upward climb speed can be capped, but the cap should not destroy carried momentum needed for tech chains.

### Non-Upward Climbing

- Non-upward climbing covers slipping, downward climb, and sideways climb.
- Non-upward climb has a timer. Apex uses a 1 second timer; our current 0.25 second grace is only a placeholder.
- The timer starts at attach and is evaluated against local-up velocity.
- If the player remains below the upward threshold after the grace expires, detach.
- If the player was climbing upward and later drops below threshold after the timer has expired, detach.

### Slipping

- Slipping is a valid climb sub-state, not an immediate failure.
- Slipping happens when:
  - player attached from momentum but gives no climb input;
  - input is neutralized, such as forward and backward together;
  - player releases movement input;
  - gravity or incoming downward velocity overpowers climb force;
  - sideways climb is aimed so the combined movement vector no longer points into the wall.
- Slipping should move local-down along the wall and preserve relevant horizontal momentum.

### Downward Climb

- Downward climb is different from passive slipping and should be faster/intentional.
- If movement direction points away from the wall, climb should move downward along the wall instead of immediately detaching, as long as look/surface validity remains within climb limits.
- Back input can be a hard detach if we want stronger readability, but Apex-style behavior distinguishes backward/downward intent from immediate invalid detach in several input combinations.

### Sideways Climb

- Sideways climb is a non-upward climb variant.
- It should use the same non-up timer.
- It requires enough contact/momentum into the wall.
- Horizontal speed should be preserved and can become a setup for wall push, wallbounce, U-bounce-like behavior, or wallrun handoff.
- Looking slightly opposite the sideways input helps sustain sideways motion; looking into the same direction can trigger slip/downward behavior.

## Detach Rules

### Normal Detaches

Normal detaches should apply climb-space / reattach penalties:

- crouch;
- jump;
- back input, if we keep this as a hard detach;
- wall becomes invalid or no longer detected;
- surface angle becomes too shallow;
- view angle exceeds the climb angle limit;
- non-upward timer expires;
- attach offset is exhausted;
- climb space cutoff is reached.

### Abnormal Detaches

Abnormal detaches should not apply the same penalties:

- direct transition into mantle/ledge grab;
- unclimbable special objects, if we add climb material tags;
- side-of-wall drop cases where the player simply loses geometry.

## End Boost

- Climb should split into climb phase and end-boost phase.
- End boost triggers when reaching attach offset or climb-space cutoff.
- It adds a small local-up boost and preserves carried momentum.
- Speed boosts should affect end boost more than raw climb height.
- End boost is a key bridge into superglide-style or wall tech later.

## Reattach

- In-air reattach is allowed.
- Same-wall reattach should require dropping below the previous attach point or previous attach height.
- A sufficiently different wall normal can allow a new attach point without dropping below the old one. Apex's referenced angle is about 25.842 degrees; ours should be configurable.
- Reattach must also respect climb-space validity. Passing attach-point rules alone should not be enough.
- Reattach state must store previous wall normal, previous attach point, previous attach height, and climb-space baseline/cutoff.

## Wallrun Interplay

Climb and wallrun should be connected, not competing one-frame modes.

- Wallrun into climb:
  - if velocity into the wall and local-up state are climb-valid, wallrun can convert into climb/stick near obstacles, door frames, or vertical interruption surfaces;
  - wallrun release/jump should still have priority over accidental climb.
- Climb into wallrun:
  - sideways climb can transition into wallrun if horizontal tangent speed and wallrun input are valid;
  - after non-upward climb timer is near expiry, sideways input can become wallrun-like motion if a side wall is valid.
- Wallrun around corners:
  - wallrun surface handoff should use collision-backed anchor/tangent information;
  - it must never flip 180 degrees just to remain attached;
  - at gaps or door openings, it should either hand off to a forward-compatible surface or detach.
- Wall push:
  - releasing forward in a neutral/upper climb zone, then jumping, should produce a mostly horizontal push away from wall while carrying vertical velocity.

## Implementation Implications

Required new state:

- `climbAttachPoint`
- `climbAttachHeight`
- `climbBaseline`
- `climbSpaceCutoff`
- `climbZone`
- `climbNonUpTimer`
- `climbPreviousWallNormal`
- `climbPreviousAttachHeight`
- `climbDetachPenalty`
- `climbDetachKind`
- `climbIncomingVelocity`

Required constants:

- climb angle limit;
- climbable surface minimum steepness;
- upward velocity threshold;
- non-upward climb timer;
- slip speed;
- downward climb speed;
- sideways climb max speed;
- attach offset height;
- climb space height;
- mini/green/neutral zone boundaries;
- reattach different-wall angle;
- normal detach climb-space penalty;
- jump detach penalty per zone;
- end boost local-up impulse;
- wall-push horizontal impulses per zone.

Required tests:

- attach from airborne forward input;
- attach from wallward momentum with delayed forward input;
- reject grounded attach;
- reject shallow surfaces;
- reject excessive look angle;
- upward climb has no timer while local-up velocity stays above threshold;
- non-upward climb detaches after timer;
- slip persists before timer expiry;
- downward climb moves local-down faster than slip;
- sideways climb uses non-up timer and preserves tangent speed;
- same-wall reattach blocked above previous attach point;
- different-wall reattach allowed by angle threshold;
- climb-space cutoff triggers end boost;
- attach-offset exhaustion triggers end boost;
- mantle transition does not apply normal detach penalty;
- wall push from neutral zone gives mostly horizontal velocity;
- wallrun-to-climb and climb-to-wallrun transitions do not fight each other.

