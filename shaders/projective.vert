#version 450

layout(location = 0) out vec3       fragColor;
layout(location = 1) out vec3       fragWorldPos;
layout(location = 2) out float      fragIsFloor;
layout(location = 3) flat out vec3  fragNormal;

layout(set = 1, binding = 0) uniform Matrices
{
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

// ═══════════════════════════════════════════════════════════════════════════
// Physics test playground — all geometry in world space (model = identity).
//
// Objects (vertex ranges):
//   Boxes    [0..899]    25 axis-aligned boxes × 36 verts
//   Ramps    [900..959]  2 ramps × 30 verts (wedge shapes)
//   DiagWall [960..995]  1 diagonal wall × 36 verts
//   Floor    [996..1001] 1 floor quad × 6 verts
//
// Total: 1002 vertices.
// ═══════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────
// Box geometry — 11 AABBs, procedurally generated from (min, max) pairs.
// ─────────────────────────────────────────────────────────────────────────

const int NUM_BOXES    = 25;
const int BOX_VERTS    = NUM_BOXES * 36;   // 900

const vec3 boxMinMax[50] = vec3[](
    // Box 0: reference cube
    vec3(-32, 0, 368), vec3(32, 64, 432),
    // Box 1: small steppable box
    vec3(68, 0, 768), vec3(132, 16, 832),
    // Box 2: large jumpable box
    vec3(-140, 0, 760), vec3(-60, 64, 840),
    // Box 3: stair 1
    vec3(-64, 0, 1500), vec3(64, 16, 1548),
    // Box 4: stair 2
    vec3(-64, 0, 1548), vec3(64, 32, 1596),
    // Box 5: stair 3
    vec3(-64, 0, 1596), vec3(64, 48, 1644),
    // Box 6: stair 4
    vec3(-64, 0, 1644), vec3(64, 64, 1692),
    // Box 7: stair 5
    vec3(-64, 0, 1692), vec3(64, 80, 1740),
    // Box 8: wall (axis-aligned)
    vec3(-128, 0, 1892), vec3(128, 120, 1908),
    // Box 9: pole
    vec3(-258, 0, 1892), vec3(-242, 200, 1908),
    // Box 10: thin walkway (elevated)
    vec3(-16, 80, 2100), vec3(16, 96, 2500),

    // ── Wallrun corridor ──
    // Box 11: left wallrun wall
    vec3(-116, 0, 2700), vec3(-100, 200, 3100),
    // Box 12: right wallrun wall
    vec3(100, 0, 2700), vec3(116, 200, 3100),

    // ── Wall-to-wall jump section ──
    // Box 13: left wall (offset)
    vec3(-116, 0, 3300), vec3(-100, 200, 3600),
    // Box 14: right wall (offset)
    vec3(140, 0, 3400), vec3(156, 200, 3700),

    // ── Climb wall ──
    // Box 15: tall climb wall
    vec3(-64, 0, 3900), vec3(64, 300, 3916),

    // ── Ledge wall ──
    // Box 16: medium wall with grabbable top
    vec3(200, 0, 3900), vec3(328, 120, 3916),

    // ── Slide run guide walls ──
    // Box 17: left guide
    vec3(-200, 0, 4100), vec3(-184, 40, 4600),
    // Box 18: right guide
    vec3(184, 0, 4100), vec3(200, 40, 4600),

    // ── Parkour course ──
    // Box 19: start platform
    vec3(-48, 0, 4800), vec3(48, 48, 4848),
    // Box 20: left wallrun
    vec3(-140, 0, 4900), vec3(-124, 200, 5300),
    // Box 21: right wallrun
    vec3(124, 0, 5000), vec3(140, 200, 5400),
    // Box 22: climb target
    vec3(-64, 0, 5500), vec3(64, 250, 5516),
    // Box 23: ledge platform
    vec3(200, 0, 5500), vec3(328, 100, 5516),
    // Box 24: landing pad
    vec3(-80, 0, 5550), vec3(80, 16, 5650)
);

// Per-box color.  Box 0 (reference cube) uses per-face colors instead.
const vec3 boxColors[25] = vec3[](
    vec3(0.5),              // 0: ref cube (per-face override below)
    vec3(0.9, 0.6, 0.2),   // 1: small step box — orange
    vec3(0.2, 0.7, 0.3),   // 2: large jump box — green
    vec3(0.55, 0.55, 0.60),// 3: stair 1 — light grey-blue
    vec3(0.45, 0.45, 0.50),// 4: stair 2 — dark grey-blue
    vec3(0.55, 0.55, 0.60),// 5: stair 3
    vec3(0.45, 0.45, 0.50),// 6: stair 4
    vec3(0.55, 0.55, 0.60),// 7: stair 5
    vec3(0.6, 0.6, 0.65),  // 8: wall — grey
    vec3(0.9, 0.9, 0.9),   // 9: pole — white
    vec3(0.9, 0.8, 0.2),   // 10: walkway — gold
    // Wallrun corridor — teal
    vec3(0.2, 0.6, 0.7),   // 11: left wallrun wall
    vec3(0.2, 0.6, 0.7),   // 12: right wallrun wall
    // Wall-to-wall section — cyan
    vec3(0.3, 0.7, 0.8),   // 13: left wall offset
    vec3(0.3, 0.7, 0.8),   // 14: right wall offset
    // Climb wall — dark blue
    vec3(0.2, 0.3, 0.7),   // 15: tall climb wall
    // Ledge wall — warm brown
    vec3(0.7, 0.5, 0.3),   // 16: ledge wall
    // Slide guide walls — dark grey
    vec3(0.35, 0.35, 0.4), // 17: left guide
    vec3(0.35, 0.35, 0.4), // 18: right guide
    // Parkour course
    vec3(0.8, 0.4, 0.6),   // 19: start platform — pink
    vec3(0.2, 0.6, 0.7),   // 20: left wallrun — teal
    vec3(0.2, 0.6, 0.7),   // 21: right wallrun — teal
    vec3(0.2, 0.3, 0.7),   // 22: climb target — dark blue
    vec3(0.7, 0.5, 0.3),   // 23: ledge platform — brown
    vec3(0.4, 0.8, 0.4)    // 24: landing pad — green
);

// Reference cube per-face colours (overrides boxColors[0]).
const vec3 refCubeColors[6] = vec3[](
    vec3(1.0, 0.2, 0.2),   // front  — red
    vec3(0.2, 1.0, 0.2),   // back   — green
    vec3(0.2, 0.2, 1.0),   // left   — blue
    vec3(1.0, 1.0, 0.2),   // right  — yellow
    vec3(1.0, 0.2, 1.0),   // top    — magenta
    vec3(0.2, 1.0, 1.0)    // bottom — cyan
);

// Standard axis-aligned box face normals (same order for every box).
const vec3 boxFaceNormals[6] = vec3[](
    vec3( 0,  0,  1),   // 0: front  (+Z)
    vec3( 0,  0, -1),   // 1: back   (-Z)
    vec3(-1,  0,  0),   // 2: left   (-X)
    vec3( 1,  0,  0),   // 3: right  (+X)
    vec3( 0,  1,  0),   // 4: top    (+Y)
    vec3( 0, -1,  0)    // 5: bottom (-Y)
);

// Generate a vertex position for box `boxIdx` at local vertex `lv` ∈ [0..35].
vec3 boxVertex(int boxIdx, int lv) {
    vec3 mn = boxMinMax[boxIdx * 2];
    vec3 mx = boxMinMax[boxIdx * 2 + 1];

    int face = lv / 6;
    int vi   = lv % 6;

    // 4 corners per face (CCW when viewed from outside).
    vec3 c[4];
    switch (face) {
    case 0: // front (+Z)
        c[0] = vec3(mn.x, mn.y, mx.z); c[1] = vec3(mx.x, mn.y, mx.z);
        c[2] = vec3(mx.x, mx.y, mx.z); c[3] = vec3(mn.x, mx.y, mx.z); break;
    case 1: // back (-Z)
        c[0] = vec3(mx.x, mn.y, mn.z); c[1] = vec3(mn.x, mn.y, mn.z);
        c[2] = vec3(mn.x, mx.y, mn.z); c[3] = vec3(mx.x, mx.y, mn.z); break;
    case 2: // left (-X)
        c[0] = vec3(mn.x, mn.y, mn.z); c[1] = vec3(mn.x, mn.y, mx.z);
        c[2] = vec3(mn.x, mx.y, mx.z); c[3] = vec3(mn.x, mx.y, mn.z); break;
    case 3: // right (+X)
        c[0] = vec3(mx.x, mn.y, mx.z); c[1] = vec3(mx.x, mn.y, mn.z);
        c[2] = vec3(mx.x, mx.y, mn.z); c[3] = vec3(mx.x, mx.y, mx.z); break;
    case 4: // top (+Y)
        c[0] = vec3(mn.x, mx.y, mx.z); c[1] = vec3(mx.x, mx.y, mx.z);
        c[2] = vec3(mx.x, mx.y, mn.z); c[3] = vec3(mn.x, mx.y, mn.z); break;
    default: // bottom (-Y)
        c[0] = vec3(mn.x, mn.y, mn.z); c[1] = vec3(mx.x, mn.y, mn.z);
        c[2] = vec3(mx.x, mn.y, mx.z); c[3] = vec3(mn.x, mn.y, mx.z); break;
    }

    // 6 verts per face: tri0 = (c0,c1,c2), tri1 = (c0,c2,c3).
    const int idx[6] = int[](0, 1, 2, 0, 2, 3);
    return c[idx[vi]];
}

// ─────────────────────────────────────────────────────────────────────────
// Ramp geometry — 2 wedge ramps, 30 verts each (5 faces × 6).
//
// A ramp is a right-triangular prism:
//   A0,A1 = front-bottom edge (at y=0, z=zMin)
//   B0,B1 = back-bottom edge  (at y=0, z=zMax)
//   C0,C1 = back-top edge     (at y=h,  z=zMax)
//
// Side triangles are padded to 6 verts (degenerate second triangle).
// ─────────────────────────────────────────────────────────────────────────

const int RAMP_START   = BOX_VERTS;        // 900
const int RAMP_VERTS   = 60;               // 2 ramps × 30

// Gentle ramp (15 deg): x ∈ [-214,-86], z ∈ [950,1250], h=80
// Steep ramp  (40 deg): x ∈ [86,214],   z ∈ [1000,1200], h=168
const vec3 rampPositions[60] = vec3[](

    // ── Gentle ramp ────────────────────────────────────────────────────
    // A0=(-214,0,950)  A1=(-86,0,950)
    // B0=(-214,0,1250) B1=(-86,0,1250)
    // C0=(-214,80,1250) C1=(-86,80,1250)

    // Face 0: bottom   (normal: 0,-1,0)
    vec3(-214,0,950), vec3(-86,0,950), vec3(-86,0,1250),
    vec3(-214,0,950), vec3(-86,0,1250), vec3(-214,0,1250),

    // Face 1: back wall (normal: 0,0,1)
    vec3(-214,0,1250), vec3(-86,0,1250), vec3(-86,80,1250),
    vec3(-214,0,1250), vec3(-86,80,1250), vec3(-214,80,1250),

    // Face 2: slope     (normal: 0, 0.966, -0.258)
    vec3(-214,0,950), vec3(-214,80,1250), vec3(-86,80,1250),
    vec3(-214,0,950), vec3(-86,80,1250), vec3(-86,0,950),

    // Face 3: left side  (normal: -1,0,0) — padded
    vec3(-214,0,950), vec3(-214,0,1250), vec3(-214,80,1250),
    vec3(-214,0,950), vec3(-214,80,1250), vec3(-214,80,1250),

    // Face 4: right side (normal: 1,0,0) — padded
    vec3(-86,0,950), vec3(-86,80,1250), vec3(-86,0,1250),
    vec3(-86,0,950), vec3(-86,80,1250), vec3(-86,80,1250),

    // ── Steep ramp ─────────────────────────────────────────────────────
    // A0=(86,0,1000)   A1=(214,0,1000)
    // B0=(86,0,1200)   B1=(214,0,1200)
    // C0=(86,168,1200) C1=(214,168,1200)

    // Face 0: bottom   (normal: 0,-1,0)
    vec3(86,0,1000), vec3(214,0,1000), vec3(214,0,1200),
    vec3(86,0,1000), vec3(214,0,1200), vec3(86,0,1200),

    // Face 1: back wall (normal: 0,0,1)
    vec3(86,0,1200), vec3(214,0,1200), vec3(214,168,1200),
    vec3(86,0,1200), vec3(214,168,1200), vec3(86,168,1200),

    // Face 2: slope     (normal: 0, 0.766, -0.643)
    vec3(86,0,1000), vec3(86,168,1200), vec3(214,168,1200),
    vec3(86,0,1000), vec3(214,168,1200), vec3(214,0,1000),

    // Face 3: left side  (normal: -1,0,0) — padded
    vec3(86,0,1000), vec3(86,0,1200), vec3(86,168,1200),
    vec3(86,0,1000), vec3(86,168,1200), vec3(86,168,1200),

    // Face 4: right side (normal: 1,0,0) — padded
    vec3(214,0,1000), vec3(214,168,1200), vec3(214,0,1200),
    vec3(214,0,1000), vec3(214,168,1200), vec3(214,168,1200)
);

// Per-face-group data for ramps (10 face groups: 5 per ramp).
const vec3 rampColors[10] = vec3[](
    // Gentle ramp — green tones
    vec3(0.25, 0.65, 0.30), // bottom
    vec3(0.25, 0.65, 0.30), // back
    vec3(0.30, 0.80, 0.35), // slope (brighter)
    vec3(0.25, 0.65, 0.30), // left
    vec3(0.25, 0.65, 0.30), // right
    // Steep ramp — red-orange tones
    vec3(0.80, 0.35, 0.20), // bottom
    vec3(0.80, 0.35, 0.20), // back
    vec3(0.95, 0.45, 0.25), // slope (brighter)
    vec3(0.80, 0.35, 0.20), // left
    vec3(0.80, 0.35, 0.20)  // right
);

const vec3 rampNormals[10] = vec3[](
    // Gentle ramp (depth=300, h=80)
    vec3( 0, -1,  0),                           // bottom
    vec3( 0,  0,  1),                           // back
    vec3( 0.0, 0.966, -0.258),                  // slope
    vec3(-1,  0,  0),                           // left
    vec3( 1,  0,  0),                           // right
    // Steep ramp (depth=200, h=168)
    vec3( 0, -1,  0),                           // bottom
    vec3( 0,  0,  1),                           // back
    vec3( 0.0, 0.766, -0.643),                  // slope
    vec3(-1,  0,  0),                           // left
    vec3( 1,  0,  0)                            // right
);

// ─────────────────────────────────────────────────────────────────────────
// Diagonal wall (45 deg) — 36 verts (6 faces × 6).
//
// Centre=(300,0,1900), half-length=100, half-thickness=8, height=120
// Direction d = normalize(1,0,1) = (0.7071, 0, 0.7071)
// Face normal n = (0.7071, 0, -0.7071)
//
// Base corners (y=0):
//   A = centre + n*8 - d*100 = (235.0,   0, 1823.6)  front-left
//   B = centre + n*8 + d*100 = (376.4,   0, 1965.0)  front-right
//   C = centre - n*8 + d*100 = (365.0,   0, 1976.4)  back-right
//   D = centre - n*8 - d*100 = (223.6,   0, 1835.0)  back-left
// Top corners: same XZ at y=120.
// ─────────────────────────────────────────────────────────────────────────

const int DIAG_START   = RAMP_START + RAMP_VERTS;  // 960
const int DIAG_VERTS   = 36;

const vec3 diagPositions[36] = vec3[](

    // A=(235,0,1823.6) B=(376.4,0,1965) C=(365,0,1976.4) D=(223.6,0,1835)
    // At=(235,120,1823.6) Bt=(376.4,120,1965) Ct=(365,120,1976.4) Dt=(223.6,120,1835)

    // Face 0: front   (normal ≈ 0.707, 0, -0.707)
    vec3(235,0,1823.6), vec3(376.4,120,1965), vec3(376.4,0,1965),
    vec3(235,0,1823.6), vec3(235,120,1823.6), vec3(376.4,120,1965),

    // Face 1: back    (normal ≈ -0.707, 0, 0.707)
    vec3(365,0,1976.4), vec3(223.6,120,1835), vec3(223.6,0,1835),
    vec3(365,0,1976.4), vec3(365,120,1976.4), vec3(223.6,120,1835),

    // Face 2: left end (normal ≈ -0.707, 0, -0.707)
    vec3(223.6,0,1835), vec3(235,120,1823.6), vec3(235,0,1823.6),
    vec3(223.6,0,1835), vec3(223.6,120,1835), vec3(235,120,1823.6),

    // Face 3: right end (normal ≈ 0.707, 0, 0.707)
    vec3(376.4,0,1965), vec3(365,120,1976.4), vec3(365,0,1976.4),
    vec3(376.4,0,1965), vec3(376.4,120,1965), vec3(365,120,1976.4),

    // Face 4: top     (normal: 0, 1, 0)
    vec3(235,120,1823.6), vec3(365,120,1976.4), vec3(376.4,120,1965),
    vec3(235,120,1823.6), vec3(223.6,120,1835), vec3(365,120,1976.4),

    // Face 5: bottom  (normal: 0, -1, 0)
    vec3(235,0,1823.6), vec3(376.4,0,1965), vec3(365,0,1976.4),
    vec3(235,0,1823.6), vec3(365,0,1976.4), vec3(223.6,0,1835)
);

const vec3 diagColor = vec3(0.7, 0.3, 0.8);   // purple

const vec3 diagNormals[6] = vec3[](
    vec3( 0.7071, 0, -0.7071),  // front
    vec3(-0.7071, 0,  0.7071),  // back
    vec3(-0.7071, 0, -0.7071),  // left end
    vec3( 0.7071, 0,  0.7071),  // right end
    vec3( 0,  1,  0),           // top
    vec3( 0, -1,  0)            // bottom
);

// ─────────────────────────────────────────────────────────────────────────
// Floor quad — 6 verts.
// ─────────────────────────────────────────────────────────────────────────

const int FLOOR_START  = DIAG_START + DIAG_VERTS;   // 996
const int TOTAL_VERTS  = FLOOR_START + 6;            // 1002

const vec3 floorVerts[6] = vec3[](
    vec3(-8000, 0, -8000), vec3( 8000, 0,  8000), vec3( 8000, 0, -8000),
    vec3(-8000, 0, -8000), vec3(-8000, 0,  8000), vec3( 8000, 0,  8000)
);

// ═══════════════════════════════════════════════════════════════════════════

void main()
{
    vec3  pos;
    vec3  color;
    vec3  normal;
    float isFloor = 0.0;

    int vi = gl_VertexIndex;

    if (vi < BOX_VERTS) {
        // ── Box geometry ────────────────────────────────────────────────
        int boxIdx   = vi / 36;
        int localVi  = vi % 36;
        int face     = localVi / 6;

        pos    = boxVertex(boxIdx, localVi);
        normal = boxFaceNormals[face];

        if (boxIdx == 0)
            color = refCubeColors[face];      // per-face for reference cube
        else
            color = boxColors[boxIdx];        // solid colour per object
    }
    else if (vi < RAMP_START + RAMP_VERTS) {
        // ── Ramp geometry ───────────────────────────────────────────────
        int rampVi    = vi - RAMP_START;
        int faceGroup = rampVi / 6;           // 0..9

        pos    = rampPositions[rampVi];
        color  = rampColors[faceGroup];
        normal = rampNormals[faceGroup];
    }
    else if (vi < DIAG_START + DIAG_VERTS) {
        // ── Diagonal wall geometry ──────────────────────────────────────
        int diagVi = vi - DIAG_START;
        int face   = diagVi / 6;

        pos    = diagPositions[diagVi];
        color  = diagColor;
        normal = diagNormals[face];
    }
    else {
        // ── Floor ───────────────────────────────────────────────────────
        int floorVi = vi - FLOOR_START;

        pos     = floorVerts[floorVi];
        color   = vec3(0.4);
        normal  = vec3(0, 1, 0);
        isFloor = 1.0;
    }

    vec4 worldPos = ubo.model * vec4(pos, 1.0);
    gl_Position   = ubo.projection * ubo.view * worldPos;
    fragColor     = color;
    fragWorldPos  = worldPos.xyz;
    fragIsFloor   = isFloor;
    fragNormal    = normal;
}
