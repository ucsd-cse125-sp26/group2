// tonemap.frag — HDR → LDR tone mapping with bloom, SSAO, SSR, volumetrics composite.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D hdrBuffer;
layout(set = 2, binding = 1) uniform sampler2D bloomBuffer;
layout(set = 2, binding = 2) uniform sampler2D ssaoBuffer;
layout(set = 2, binding = 3) uniform sampler2D ssrBuffer;
layout(set = 2, binding = 4) uniform sampler2D volumetricBuffer;

layout(set = 3, binding = 0) uniform TonemapParams
{
    float exposure;
    float gamma;
    int   tonemapMode;  // 0 = ACES, 1 = Reinhard, 2 = linear
    float bloomStrength;
    float ssaoStrength;
    float ssrStrength;
    float volumetricStrength;
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

vec3 Reinhard(vec3 x)
{
    return x / (x + vec3(1.0));
}

void main()
{
    vec3 hdr = texture(hdrBuffer, fragTexCoord).rgb;

    // Composite bloom (additive).
    vec3 bloom = texture(bloomBuffer, fragTexCoord).rgb;
    hdr += bloom * bloomStrength;

    // Composite SSR (additive, alpha-weighted).
    vec4 ssr = texture(ssrBuffer, fragTexCoord);
    hdr = mix(hdr, ssr.rgb, ssr.a * ssrStrength);

    // Composite volumetrics (additive).
    vec4 vol = texture(volumetricBuffer, fragTexCoord);
    hdr += vol.rgb * volumetricStrength;

    // Apply SSAO (multiplicative on the result).
    float ao = texture(ssaoBuffer, fragTexCoord).r;
    hdr *= mix(1.0, ao, ssaoStrength);

    // Apply exposure.
    hdr *= exposure;

    // Tone map.
    vec3 ldr;
    if (tonemapMode == 0)
        ldr = ACESFilm(hdr);
    else if (tonemapMode == 1)
        ldr = Reinhard(hdr);
    else
        ldr = clamp(hdr, 0.0, 1.0);

    // Gamma correction.
    ldr = pow(ldr, vec3(1.0 / gamma));

    outColor = vec4(ldr, 1.0);
}
