/// @file lightning_arc.frag
/// @brief Lightning arc fragment shader with multi-layer cross-section glow.
#version 450

// edge: +1 = left strip edge, -1 = right strip edge.
// The centreline maps to edge ~ 0, so t = 1 - abs(edge) gives 1 at centre.
layout(location = 0) in  float vEdge;
layout(location = 1) in  vec4  vColor;  // rgb = hue, a = per-strip base alpha
layout(location = 0) out vec4  outColor;

void main()
{
    float d = abs(vEdge);   // 0 at centreline, 1 at edges

    // Cross-section profile
    //
    // Three terms at very different widths:
    //
    //   gaussGlow  -- wide Gaussian halo (sigma~0.35)  simulates atmospheric scatter
    //   innerGlow  -- exp(-d^2 * 28) Gaussian           the concentrated energy channel
    //   spike      -- very tight power-law              white-hot centreline overdriven
    //
    // All are zero-preserving at the edges and peak at d=0,
    // so the strip radius controls how wide each layer appears on screen.

    float gaussGlow = exp(-d * d * 5.0);     // broad soft halo
    float innerGlow = exp(-d * d * 28.0);    // tight bright channel
    float spike     = pow(max(0.0, 1.0 - d * 18.0), 3.0);  // sub-pixel hot pin

    // Base colour modulated by the wide glow term
    vec3 rgb = vColor.rgb * gaussGlow;

    // The inner core and spike are multiplied by vColor.a so that:
    //   - The bloom layer (a ~ 0.07) stays very dim -- it's just a halo
    //   - The glow layer  (a ~ 0.55) brightens moderately
    //   - The core layer  (a ~ 0.96) fully blows out to white -- the HDR look
    float a = vColor.a;

    rgb += vec3(0.50, 0.82, 1.00) * innerGlow * 2.6 * a;  // blue-white channel
    rgb += vec3(1.00, 1.00, 1.00) * spike      * 5.0 * a;  // white-hot pin

    float alpha = a * gaussGlow;

    outColor = vec4(rgb, alpha);
}
