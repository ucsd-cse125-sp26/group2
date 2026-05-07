# VOIDFALL HUD icon system

All vector glyphs used by the in-match HUD live in this folder.  The runtime
currently rasterises them procedurally through `src/client/hud/HudIcons.cpp`
(no SVG parser needed at boot), but the SVG files are the source of truth —
the procedural code mirrors them verbatim, and a future build step will
rasterise the SVGs into the existing icon atlas slot in `HudRenderer`.

## Icon list

| File | Used by | Notes |
|------|---------|-------|
| `hp.svg` | `HealthArmorBar`, `KillFeed` | Filled medical-cross. |
| `shield.svg` | `HealthArmorBar` | Stroked heater shield. |
| `skull.svg` | `KillFeed` (death entries) | Filled stylised skull. |
| `headshot.svg` | `KillFeed`, `HitMarkerWidget` | Reticle (ring + pip + tick). |
| `gravity.svg` | `GravityIndicator` | Stroked down-arrow; widget rotates by direction. |
| `fall.svg` | `KillFeed` (`FALL` weapon kills) | Upward filled triangle. |
| `grenade.svg` | `EquipmentSlots`, `KillFeed` | Round body + collar + lever. |
| `grapple.svg` | `EquipmentSlots` | Multi-stroke hook + rope. |
| `tactical.svg` | `EquipmentSlots` | Ring + center pip. |
| `player_arrow.svg` | `Minimap` | Notched chevron, local player. |
| `enemy_diamond.svg` | `Minimap` | Filled diamond, enemy ping. |

### To-do (procedural fallback present, SVG asset still missing)

| File | Used by | Status |
|------|---------|--------|
| `weapon_rifle.svg` | `WeaponPanel` (silhouette) | TBD |
| `weapon_rocket.svg` | `WeaponPanel` | TBD |
| `weapon_railgun.svg` | `WeaponPanel` | TBD |
| `weapon_energy.svg` | `WeaponPanel` | TBD |
| `ammo_box.svg` | `PickupNotification` | TBD |
| `shield_cell.svg` | `PickupNotification` | TBD |
| `health_cell.svg` | `PickupNotification` | TBD |

## Authoring rules

1. **Viewbox** is the icon's *natural* pixel size — typically 12×12 or 14×14.
   The procedural code in `HudIcons.cpp` divides the requested render size
   by 14 (or 12) to derive a unit `u`, then multiplies every coordinate
   from the SVG by `u`.  Match this convention so future SVG → atlas
   rasterisation produces the same on-screen layout.
2. **Color** is always `currentColor` so the widget caller can tint the
   icon (amber for ready, dim for cooldown, etc.).  Don't bake colors.
3. **No CSS, no animations, no `<style>` blocks.**  The eventual SVG
   rasteriser (nanosvg) doesn't honour them, and the procedural path
   doesn't read SVG at all.
4. **Strokes use integer-px thicknesses** (`1.2`, `1.4`).  Anything thinner
   gets antialiased away when scaled small in the procedural path.
5. **Round joins/caps for hand-drawn feel.**  The procedural path emits
   axis-aligned rotated rects — corners are squared automatically — so
   joins stay tight without shimmering.

## Adding a new icon

1. Drop a new `<name>.svg` in this folder following the rules above.
2. Add the function signature to `src/client/hud/HudIcons.hpp`:

   ```cpp
   void newIcon(HudContext& ctx, float x, float y, float size, HudColor color);
   ```

3. Implement it in `HudIcons.cpp` using the existing primitives
   (`triangle`, `polyline`, `filledCircle`, `strokedCircle`,
   `filledPolygon`, `strokedPolygon`).
4. Update the table above.
5. Call it from a widget: `voidfall::icons::newIcon(ctx, x, y, size, color);`.

## Future migration to atlas-based SVG rasterisation

`HudRenderer` already binds an `iconAtlas` sampler at fragment set 2,
binding 1 (currently a 1×1 white fallback texture).  The migration plan:

1. Add **nanosvg** (single-header) via `FetchContent` in `CMakeLists.txt`.
2. At HUD init, walk `assets/hud_icons/*.svg` and rasterise each into a
   single CPU bitmap atlas (e.g. 256×256 R8_UNORM with rectpacking).
3. Upload the bitmap to a real GPU texture and replace the 1×1 fallback
   in `HudRenderer::iconAtlasTex_`.
4. Build a `HudIcon` enum → UV-rect lookup table and call
   `HudContext::icon(HudIcon::Shield, x, y, size, color)` from the
   procedural functions instead of emitting triangles directly.

After step 4 the procedural fallbacks remain as a safety net for icons
whose SVG hasn't been authored yet, but the production path is fully
texture-based.
