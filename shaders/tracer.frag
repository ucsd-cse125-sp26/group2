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

    // Two-layer profile: solid core + tight falloff
    float core = 1.0 - smoothstep(0.0, 0.25, dist);  // bright centre
    float body = 1.0 - smoothstep(0.0, 0.50, dist);  // visible body
    float edge = 1.0 - smoothstep(0.20, 0.70, dist); // soft outer edge

    // Tip-to-tail brightness: tail stays visible
    float tipFade = 0.45 + 0.55 * vUV.x;

    // Base color — fully saturated orange, not washed out
    vec3 baseRGB = mix(vEdgeColor.rgb, vCoreColor.rgb, body);

    // HDR push — values well above 1.0 trigger bloom, making the tracer
    // glow visibly even against bright lit surfaces.
    vec3 hdrColor = baseRGB * 3.0 * vBrightness * tipFade;
    hdrColor += vec3(2.5, 1.0, 0.2) * core * vBrightness * tipFade;

    // Nearly fully opaque across the entire body so it reads as a solid
    // streak, not a ghostly overlay. Only the outer fringe fades.
    float alpha = mix(edge * 0.8, 1.0, body) * vBrightness * tipFade;

    outColor = vec4(hdrColor, alpha);
}
