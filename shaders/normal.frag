// normal.frag — PBR-consistent lighting for hard-coded scene geometry.
// Matches the primary directional light, receives shadows, procedural grid floor.
#version 450

layout(location = 0) in  vec3       fragColor;
layout(location = 1) in  vec3       fragWorldPos;
layout(location = 2) in  float      fragIsFloor;
layout(location = 3) flat in vec3   fragNormal;

layout(location = 0) out vec4 outColor;

// Shadow map (comparison sampler for hardware PCF).
layout(set = 2, binding = 0) uniform sampler2DShadow shadowMap;

// Shadow + lighting data (pushed per-frame, matches ShadowDataFragUBO in C++).
layout(set = 3, binding = 0) uniform SceneShadowData
{
    mat4  lightVP;
    float shadowBias;
    float shadowNormalBias;
    float shadowMapSize;
    float _pad;
};

// ── Lighting constants (match PBR pipeline's primary directional light) ────
const vec3  lightDirToLight = normalize(vec3(0.5, 0.3, 0.8));
const vec3  lightColor      = vec3(1.0, 0.95, 0.85);
const float lightIntensity  = 3.0;
const float ambient         = 0.06;   // low ambient for punchy contrast

// ── Shadow sampling (3x3 PCF, matches pbr.frag) ───────────────────────────
float calcShadow(vec3 worldPos)
{
    vec4 lightClip = lightVP * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / lightClip.w;
    vec2 shadowUV = ndc.xy * 0.5 + 0.5;

    // Outside shadow map → fully lit.
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;

    float currentDepth = ndc.z - shadowBias;
    float texelSize = 1.0 / shadowMapSize;
    float shadow = 0.0;

    // 3x3 PCF kernel.
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            shadow += texture(shadowMap, vec3(shadowUV + offset, currentDepth));
        }
    }
    return shadow / 9.0;
}

void main()
{
    vec3 N = normalize(fragNormal);
    float NdotL = max(dot(N, lightDirToLight), 0.0);

    // Shadow.
    float shadow = (shadowMapSize > 0.0) ? calcShadow(fragWorldPos) : 1.0;

    // Lighting: strong directional + low ambient for crisp shadows.
    vec3 lighting = lightColor * lightIntensity * NdotL * shadow + vec3(ambient);

    if (fragIsFloor > 0.5) {
        // ── Red matte floor with thin black grid lines ─────────────────────
        // 200-unit tiles, matching the reference blockout look.
        vec2 tileUV = fragWorldPos.xz / 200.0;
        vec2 grid   = abs(fract(tileUV) - 0.5);
        float line  = min(grid.x, grid.y);

        // Thin black grid lines at tile boundaries.
        float gridMask = smoothstep(0.005, 0.015, line);

        // Warm salmon/red base, black at grid lines.
        vec3 base = mix(vec3(0.02), vec3(0.75, 0.30, 0.25), gridMask);

        outColor = vec4(base * lighting, 1.0);
    } else {
        outColor = vec4(fragColor * lighting, 1.0);
    }
}
