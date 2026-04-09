// normal.frag
#version 450

layout(location = 0) in  vec3  fragColor;
layout(location = 1) in  vec3  fragWorldPos;
layout(location = 2) in  float fragIsFloor;

layout(location = 0) out vec4 outColor;

void main()
{
    if (fragIsFloor > 0.5) {
        // Checkerboard pattern: 100-unit tiles in world XZ.
        // At ground speed (~400 u/s) you cross a tile every 0.25 s — very readable.
        ivec2 cell    = ivec2(floor(fragWorldPos.xz / 100.0));
        float checker = float((cell.x + cell.y) & 1);
        outColor = vec4(mix(vec3(0.18), vec3(0.34), checker), 1.0);
    } else {
        outColor = vec4(fragColor, 1.0);
    }
}
