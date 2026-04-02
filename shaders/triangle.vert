#version 450

layout(location = 0) out vec3 fragColor;

// Player position pushed from the CPU each frame (slot 0, set 1).
// Convention: +X = right, +Y = up.  Y is negated here because Vulkan NDC
// has +Y pointing down the screen.
layout(set = 1, binding = 0) uniform PlayerOffset
{
    vec2 offset;
};

// Hardcoded RGB triangle — no vertex buffer needed
const vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // red
    vec3(0.0, 1.0, 0.0), // green
    vec3(0.0, 0.0, 1.0)  // blue
);

void main()
{
    // Negate Y so positive player.y moves up on screen (game-space +Y = up).
    gl_Position = vec4(positions[gl_VertexIndex] + vec2(offset.x, -offset.y), 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
