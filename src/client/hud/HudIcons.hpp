/// @file HudIcons.hpp
/// @brief Procedural vector icons for the VOIDFALL HUD.
///
/// Every icon takes (ctx, x, y, size, color) and renders into a `size × size`
/// box anchored at top-left.  The geometry is emitted as triangles + polylines
/// through the existing HudContext primitives, so icons are crisp at any DPR
/// and don't depend on texture filtering, mip chains, or atlas packing.
///
/// ## Why this lives here, not in widgets
/// The previous implementation built every icon ad-hoc inside its host widget
/// using rotated rectangles, which produced the chunky / mis-aligned shield
/// and grapple seen in early playtesting.  Centralising the geometry here:
///   - lets every widget reach the same icon by name,
///   - keeps the visual language consistent (proportions match the design's
///     12-14 px SVG glyphs),
///   - decouples icon authoring from widget layout.
///
/// ## Future SVG migration
/// The function signatures intentionally match the shape of an SVG-atlas
/// lookup: `draw(ctx, x, y, size, color)`.  When an SVG raster pipeline
/// (nanosvg + GPU atlas) is added, each function body can be swapped to a
/// single `ctx.icon(HudIcon::X, x, y, size, color)` without touching any
/// caller — the procedural path becomes a fallback for missing SVGs.
///
/// ## Adding a new icon
/// 1. Add the function signature here.
/// 2. Implement it in `HudIcons.cpp` using `triangle()`, `polyline()`,
///    `filledCircle()`, or `strokedCircle()`.
/// 3. Drop a matching `<name>.svg` into `assets/hud_icons/` (12×12 px,
///    `currentColor` strokes/fills) so the future atlas pipeline picks it up.

#pragma once

#include "HudTypes.hpp"

class HudContext;

namespace voidfall::icons
{

// ── Shared shape primitives ────────────────────────────────────────────────

/// @brief Fill a closed polygon with `n` points using a triangle fan from
///        the first vertex.  Points are flat (x0,y0,x1,y1,...).
void filledPolygon(HudContext& ctx, const float* points, int n, HudColor color);

/// @brief Stroke a closed polygon (connects last point back to first).
void strokedPolygon(HudContext& ctx, const float* points, int n, float thickness, HudColor color);

/// @brief Fill a regular polygon approximating a circle.
void filledCircle(HudContext& ctx, float cx, float cy, float r, int sides, HudColor color);

/// @brief Stroke a circle as a `sides`-segment ring.
void strokedCircle(HudContext& ctx, float cx, float cy, float r, float thickness, int sides, HudColor color);

// ── Named glyphs (size = bounding-box edge length, in pixels) ─────────────

/// @brief Medical-cross HP glyph (filled).
void hp(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Shield silhouette: top-flat pentagon with curved bottom (stroked).
void shield(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Skull silhouette (filled).
void skull(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Headshot reticle: circle + center pip + line below.
void headshot(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Gravity arrow inside a small dial.  `direction` is 0=down, 1=left, 2=up, 3=right.
void gravityArrow(HudContext& ctx, float x, float y, float size, int direction, HudColor color);

/// @brief Upward filled triangle (used for "FALL" weapon kill icon).
void fall(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Grenade: round body + collar + lever (filled silhouette).
void grenade(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Grapple hook: hook shape + diagonal rope (stroked).
void grapple(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Tactical: dotted ring + center pip (stroked).
void tactical(HudContext& ctx, float x, float y, float size, HudColor color);

/// @brief Player arrow chevron used on the minimap (filled, with notch).
void playerArrow(HudContext& ctx, float cx, float cy, float size, HudColor color);

/// @brief Diamond marker for enemy positions on the minimap.
void enemyDiamond(HudContext& ctx, float cx, float cy, float size, HudColor color);

} // namespace voidfall::icons
