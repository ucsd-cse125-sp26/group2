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

// ---------------------------------------------------------------------------
// All geometry is stored in world space so model = identity each frame.
//
// Vertices 0-35  : reference cube (64-unit cube, bottom at y=0, centred at
//                  x=0,z=400 — visible straight ahead from spawn).
// Vertices 36-41 : floor quad (4000x4000 units at y=0, centred at origin).
// ---------------------------------------------------------------------------

const vec3 positions[42] = vec3[](

    // --- Cube (36 verts, 6 faces × 2 tris × 3 verts) ---
    // Each original ±0.5 vertex is scaled ×64 then offset by (0,+32,+400)
    // so the cube sits on the floor (bottom y=0) at world (0,32,400).

    // Front face  (z = 432)
    vec3(-32,  0, 432), vec3( 32,  0, 432), vec3( 32, 64, 432),
    vec3(-32,  0, 432), vec3( 32, 64, 432), vec3(-32, 64, 432),

    // Back face   (z = 368)
    vec3( 32,  0, 368), vec3(-32,  0, 368), vec3(-32, 64, 368),
    vec3( 32,  0, 368), vec3(-32, 64, 368), vec3( 32, 64, 368),

    // Left face   (x = -32)
    vec3(-32,  0, 368), vec3(-32,  0, 432), vec3(-32, 64, 432),
    vec3(-32,  0, 368), vec3(-32, 64, 432), vec3(-32, 64, 368),

    // Right face  (x = +32)
    vec3( 32,  0, 432), vec3( 32,  0, 368), vec3( 32, 64, 368),
    vec3( 32,  0, 432), vec3( 32, 64, 368), vec3( 32, 64, 432),

    // Top face    (y = 64)
    vec3(-32, 64, 432), vec3( 32, 64, 432), vec3( 32, 64, 368),
    vec3(-32, 64, 432), vec3( 32, 64, 368), vec3(-32, 64, 368),

    // Bottom face (y = 0) — back-face culled when viewed from above
    vec3(-32,  0, 368), vec3( 32,  0, 368), vec3( 32,  0, 432),
    vec3(-32,  0, 368), vec3( 32,  0, 432), vec3(-32,  0, 432),

    // --- Floor quad (6 verts) ---
    // Covers ±2000 units in XZ, centred at world origin, flat at y = 0.
    // Counter-clockwise when viewed from above (+Y) so back-face culling keeps it.
    vec3(-2000, 0, -2000), vec3( 2000, 0,  2000), vec3( 2000, 0, -2000),
    vec3(-2000, 0, -2000), vec3(-2000, 0,  2000), vec3( 2000, 0,  2000)
);

// One colour per face-group (every 6 consecutive verts share a colour).
// Index = gl_VertexIndex / 6:
//   0-5 → face 0 (front,  red)      30-35 → face 5 (bottom, cyan)
//   6-11→ face 1 (back,   green)    36-41 → index 6 (floor – overridden
//  12-17→ face 2 (left,   blue)                       by checkerboard)
//  18-23→ face 3 (right,  yellow)
//  24-29→ face 4 (top,    magenta)
const vec3 colors[7] = vec3[](
    vec3(1.0, 0.2, 0.2),   // front  — red
    vec3(0.2, 1.0, 0.2),   // back   — green
    vec3(0.2, 0.2, 1.0),   // left   — blue
    vec3(1.0, 1.0, 0.2),   // right  — yellow
    vec3(1.0, 0.2, 1.0),   // top    — magenta
    vec3(0.2, 1.0, 1.0),   // bottom — cyan
    vec3(0.4, 0.4, 0.4)    // floor  — mid-grey (checkerboard applied in frag)
);

// Per-face outward normals — 7 entries matching the color table layout:
//   0 front (+Z), 1 back (-Z), 2 left (-X), 3 right (+X),
//   4 top (+Y),   5 bottom (-Y), 6 floor (+Y).
const vec3 normals[7] = vec3[](
    vec3( 0,  0,  1),   // front
    vec3( 0,  0, -1),   // back
    vec3(-1,  0,  0),   // left
    vec3( 1,  0,  0),   // right
    vec3( 0,  1,  0),   // top
    vec3( 0, -1,  0),   // bottom
    vec3( 0,  1,  0)    // floor
);

void main()
{
    vec4 worldPos  = ubo.model * vec4(positions[gl_VertexIndex], 1.0);
    gl_Position    = ubo.projection * ubo.view * worldPos;
    fragColor      = colors[gl_VertexIndex / 6];
    fragWorldPos   = worldPos.xyz;
    fragIsFloor    = (gl_VertexIndex >= 36) ? 1.0 : 0.0;
    fragNormal     = normals[gl_VertexIndex / 6];
}
