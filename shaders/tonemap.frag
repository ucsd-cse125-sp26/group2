// tonemap.frag — HDR → LDR tone mapping + gamma correction.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D hdrBuffer;

layout(set = 3, binding = 0) uniform TonemapParams
{
    float exposure;     // default 1.0
    float gamma;        // default 2.2
    int   tonemapMode;  // 0 = ACES filmic, 1 = Reinhard, 2 = linear (debug)
    float _pad;
};

// ACES filmic tone mapping (Narkowicz 2015).
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard tone mapping.
vec3 Reinhard(vec3 x)
{
    return x / (x + vec3(1.0));
}

void main()
{
    vec3 hdr = texture(hdrBuffer, fragTexCoord).rgb;

    // Apply exposure.
    hdr *= exposure;

    // Tone map.
    vec3 ldr;
    if (tonemapMode == 0)
        ldr = ACESFilm(hdr);
    else if (tonemapMode == 1)
        ldr = Reinhard(hdr);
    else
        ldr = clamp(hdr, 0.0, 1.0);   // linear pass-through (debug)

    // Gamma correction.
    ldr = pow(ldr, vec3(1.0 / gamma));

    outColor = vec4(ldr, 1.0);
}
