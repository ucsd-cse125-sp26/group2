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

    // Three-layer cross-section profile
    float core  = 1.0 - smoothstep(0.0, 0.10, dist); // white-hot centre line
    float mid   = 1.0 - smoothstep(0.0, 0.40, dist); // orange glow
    float glow  = 1.0 - smoothstep(0.0, 1.00, dist); // broad diffuse aura

    // Tip-to-tail brightness falloff: tip (u=1) is brightest
    float tipFade = 0.4 + 0.6 * vUV.x;

    // Base color: edge -> core
    vec4 col = mix(vEdgeColor, vCoreColor, mid) * glow * vBrightness * tipFade;

    // Overdriven white-hot core (simulates HDR bloom on LDR display)
    col.rgb += vec3(1.4, 1.3, 1.0) * core * vBrightness * tipFade * 1.5;
    col.a    = max(col.a, glow * vBrightness * tipFade);

    outColor = col;
}
