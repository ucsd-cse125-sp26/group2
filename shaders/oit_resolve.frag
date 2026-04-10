// oit_resolve.frag — Fullscreen resolve for Weighted Blended OIT.
// Composites the accumulation + revealage buffers onto the opaque background.
// Paired with fullscreen.vert.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D accumTexture;
layout(set = 2, binding = 1) uniform sampler2D revealTexture;

void main()
{
    vec4 accum = texture(accumTexture, fragTexCoord);
    float reveal = texture(revealTexture, fragTexCoord).r;

    // No transparent fragments at this pixel.
    if (accum.a < 0.001) {
        discard;
    }

    // Weighted average color.
    vec3 avgColor = accum.rgb / max(accum.a, 0.00001);

    // Composite: lerp between opaque background and transparent color
    // using the revealage (how much of the background shows through).
    outColor = vec4(avgColor, 1.0 - reveal);
}
