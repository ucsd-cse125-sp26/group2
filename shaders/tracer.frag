/// @file tracer.frag
/// @brief Bullet tracer fragment shader with three-layer cross-section profile.
#version 450

layout(location = 0) in  vec2  vUV;       // .x = 0(tail)->1(tip), .y = -1..+1 (cross-section)
layout(location = 1) in  float vBrightness;
layout(location = 2) in  vec4  vCoreColor;
layout(location = 3) in  vec4  vEdgeColor;
layout(location = 0) out vec4  outColor;

void main()
{
    float dist = abs(vUV.y);

    // Bright projectile profile: hot center plus a soft readable glow.
    float core = 1.0 - smoothstep(0.0, 0.28, dist);
    float glow = 1.0 - smoothstep(0.12, 0.95, dist);

    // Tail fades smoothly; the front half carries the visible bullet head.
    float tailFade = smoothstep(-0.05, 0.72, vUV.x);
    float headHot = smoothstep(0.45, 1.0, vUV.x);

    vec3 hdrColor = vEdgeColor.rgb * glow * 1.75;
    hdrColor += vCoreColor.rgb * core * (3.4 + 3.0 * headHot);
    hdrColor *= vBrightness * tailFade;

    float alpha =
        vBrightness * tailFade * (glow * vEdgeColor.a + core * vCoreColor.a * (0.45 + 0.55 * headHot));

    if (alpha <= 0.003)
        discard;

    outColor = vec4(hdrColor, alpha);
}
