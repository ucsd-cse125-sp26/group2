#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  float vBrightness;
layout(location = 2) in  vec4  vCoreColor;
layout(location = 3) in  vec4  vEdgeColor;
layout(location = 0) out vec4  outColor;

void main()
{
    float t = 1.0 - abs(vUV.y);

    // Three-layer beam: soft outer glow, tighter inner glow, hot white core
    float outerGlow = pow(t, 2.0);      // wide energy aura
    float innerGlow = pow(t, 6.0);      // concentrated channel
    float coreGlow  = pow(t, 20.0);     // white-hot centerline

    vec4 col = mix(vEdgeColor, vCoreColor, t) * outerGlow * vBrightness;

    // Concentrated inner channel — brighter than base
    col.rgb += vCoreColor.rgb * innerGlow * vBrightness * 1.2;

    // White-hot core overdriven for HDR look
    col.rgb += vec3(1.0, 1.2, 1.4) * coreGlow * vBrightness * 3.0;
    col.a    = max(col.a, outerGlow * vBrightness);

    outColor = col;
}
