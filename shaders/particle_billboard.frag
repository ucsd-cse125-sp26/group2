/// @file particle_billboard.frag
/// @brief Billboard particle fragment shader with streak and disc modes.
#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  vec4  vColor;
layout(location = 2) in  float vSpeedNorm;
layout(location = 0) out vec4  outColor;

void main()
{
    float alpha;

    // Hot core tinted toward the particle's own color.  For white-ish sparks
    // (metal, concrete) the +0.3 bias gives the original (1.3,1.2,0.9) values;
    // for colored particles (blood=red, energy=cyan) the core matches the tint
    // instead of washing out to white under additive blending.
    vec3 hotColor = vColor.rgb + vec3(0.3);

    if (vSpeedNorm > 0.05) {
        // Streak spark: sharp bright core fading to nothing at the tail end
        // vUV.x: -1 = back of streak (tail), +1 = front (tip)
        // vUV.y: +/-1 = cross-section edge

        float crossFade  = 1.0 - smoothstep(0.0, 1.0, abs(vUV.y));    // thin cross-section
        float tailFade   = smoothstep(-1.0, 0.0, vUV.x);               // fade toward tail
        float coreLine   = 1.0 - smoothstep(0.0, 0.25, abs(vUV.y));    // bright centerline

        alpha = crossFade * tailFade;

        vec3 hotCore     = hotColor * coreLine * tailFade;
        outColor         = vec4(vColor.rgb * alpha + hotCore, alpha);
    } else {
        // Circle spark / impact flash: soft disc with bright centre
        float dist       = length(vUV);
        float disc       = 1.0 - smoothstep(0.2, 0.5, dist);
        float hotspot    = 1.0 - smoothstep(0.0, 0.15, dist);

        alpha = disc * vColor.a;
        outColor = vec4(vColor.rgb * disc + hotColor * hotspot, alpha);
    }
}
