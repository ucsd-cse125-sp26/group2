/// @file tonemap.frag
/// @brief HDR to LDR tone mapping with bloom, SSAO, SSR, volumetrics composite.
/// Includes post-TAA sharpening (unsharp mask) for crisp output.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

/// @brief HDR scene color buffer.
layout(set = 2, binding = 0) uniform sampler2D hdrBuffer;
/// @brief Bloom buffer (additive).
layout(set = 2, binding = 1) uniform sampler2D bloomBuffer;
/// @brief Screen-space ambient occlusion buffer.
layout(set = 2, binding = 2) uniform sampler2D ssaoBuffer;
/// @brief Screen-space reflections buffer (RGB=color, A=confidence).
layout(set = 2, binding = 3) uniform sampler2D ssrBuffer;
/// @brief Volumetric light scattering buffer.
layout(set = 2, binding = 4) uniform sampler2D volumetricBuffer;

/// @brief Tone mapping and compositing parameters.
layout(set = 3, binding = 0) uniform TonemapParams
{
    float exposure;
    float gamma;
    int   tonemapMode;  // 0 = ACES, 1 = Reinhard, 2 = linear
    float bloomStrength;
    float ssaoStrength;
    float ssrStrength;
    float volumetricStrength;
    float sharpenStrength;
    float ssaoPower;
    float _padTM1, _padTM2, _padTM3;
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
    // Sharpening (unsharp mask on HDR, counters TAA blur)
    vec2 ts = 1.0 / vec2(textureSize(hdrBuffer, 0));
    vec4 centerSample = texture(hdrBuffer, fragTexCoord);
    vec3 center = centerSample.rgb;
    // Alpha carries the weapon viewmodel mask: 0 = weapon pixel, 1 = scene.
    // Screen-space post-FX (bloom, SSR, SSAO, volumetrics) were computed
    // before the weapon was drawn, so compositing them over weapon pixels
    // would bleed the scene through the gun.  Skip them for weapon pixels.
    // Hard threshold: only weapon pixels (alpha exactly 0.0) skip post-FX.
    // Particles may have partial alpha from additive blending — treat them
    // as scene so they still receive bloom, SSAO, etc.
    float sceneMask = step(0.01, centerSample.a);
    vec3 hdr;

    if (sharpenStrength > 0.0) {
        vec3 top = texture(hdrBuffer, fragTexCoord + vec2(0, -ts.y)).rgb;
        vec3 bot = texture(hdrBuffer, fragTexCoord + vec2(0,  ts.y)).rgb;
        vec3 lft = texture(hdrBuffer, fragTexCoord + vec2(-ts.x, 0)).rgb;
        vec3 rgt = texture(hdrBuffer, fragTexCoord + vec2( ts.x, 0)).rgb;
        vec3 neighbors = (top + bot + lft + rgt) * 0.25;
        hdr = max(center + (center - neighbors) * sharpenStrength, vec3(0.0));
    } else {
        hdr = center;
    }

    // Composite bloom (additive) — scene only.
    vec3 bloom = texture(bloomBuffer, fragTexCoord).rgb;
    hdr += bloom * bloomStrength * sceneMask;

    // Composite SSR — scene only.
    vec4 ssr = texture(ssrBuffer, fragTexCoord);
    hdr = mix(hdr, ssr.rgb, ssr.a * ssrStrength * sceneMask);

    // Composite volumetrics (additive) — scene only.
    vec4 vol = texture(volumetricBuffer, fragTexCoord);
    hdr += vol.rgb * volumetricStrength * sceneMask;

    // Apply SSAO (multiplicative) — scene only.
    float ao = texture(ssaoBuffer, fragTexCoord).r;
    ao = pow(ao, ssaoPower);
    hdr *= mix(1.0, ao, ssaoStrength * sceneMask);

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
