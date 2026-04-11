/// @file normal.frag
/// @brief Lit scene geometry with cascaded shadows and hemisphere ambient.
/// All lighting parameters come from the UBO (driven by ImGui sliders).
#version 450

layout(location = 0) in  vec3       fragColor;
layout(location = 1) in  vec3       fragWorldPos;
layout(location = 2) in  float      fragIsFloor;
layout(location = 3) flat in vec3   fragNormal;

layout(location = 0) out vec4 outColor;

/// @brief Cascaded shadow map atlas with comparison sampler.
layout(set = 2, binding = 0) uniform sampler2DShadow shadowMap;

/// @brief All lighting and cascade shadow data from C++ (matches ShadowDataFragUBO).
layout(set = 3, binding = 0) uniform SceneShadowData
{
    mat4  lightVP[4];      // Per-cascade light view-projection matrices.
    vec4  cascadeSplits;   // View-space far distances for each cascade.
    mat4  cameraView;      // Camera view matrix.
    float shadowBias;
    float shadowNormalBias;
    float shadowMapSize;   // Per-cascade resolution.
    float _pad;
    vec4  lightDirWorld;   // xyz = direction TO sun
    vec4  lightColor;      // rgb = sun color, a = sun intensity
    vec4  ambientColor;    // rgb = ambient color
    vec4  fillColor;       // rgb = fill color, a = fill intensity
};

// Atlas layout: 2x2 grid, each cascade occupies 0.5 of the atlas per axis.
const vec2 k_cascadeOffsets[4] = vec2[4](
    vec2(0.0, 0.0), vec2(0.5, 0.0),
    vec2(0.0, 0.5), vec2(0.5, 0.5)
);

// Shadow sampling for one cascade (3x3 PCF on atlas)
float sampleCascade(int cascade, vec3 offsetPos)
{
    vec4 lc  = lightVP[cascade] * vec4(offsetPos, 1.0);
    vec3 ndc = lc.xyz / lc.w;
    vec2 localUV = ndc.xy * 0.5 + 0.5;
    localUV.y = 1.0 - localUV.y;

    if (localUV.x < 0.0 || localUV.x > 1.0 || localUV.y < 0.0 || localUV.y > 1.0)
        return 1.0;

    vec2 atlasUV      = localUV * 0.5 + k_cascadeOffsets[cascade];
    float currentDepth = ndc.z - shadowBias;
    float texelSize    = 1.0 / (shadowMapSize * 2.0);
    float total = 0.0;

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 off = vec2(float(x), float(y)) * texelSize;
            total += texture(shadowMap, vec3(atlasUV + off, currentDepth));
        }
    }
    return total / 9.0;
}

// Cascaded shadow sampling with inter-cascade blending
float calcShadow(vec3 worldPos, vec3 N)
{
    vec3 lightDir = normalize(lightDirWorld.xyz);
    float NdotL = dot(N, lightDir);
    float normalOffsetScale = shadowNormalBias * (1.0 - NdotL);
    vec3 offsetPos = worldPos + N * normalOffsetScale;

    float viewZ = -(cameraView * vec4(worldPos, 1.0)).z;

    int cascade = -1;
    for (int i = 0; i < 4; ++i) {
        if (viewZ < cascadeSplits[i]) { cascade = i; break; }
    }
    if (cascade < 0) return 1.0;

    float shadowVal = sampleCascade(cascade, offsetPos);

    // Blend in last 10% of cascade range.
    float cStart    = (cascade > 0) ? cascadeSplits[cascade - 1] : 0.0;
    float cEnd      = cascadeSplits[cascade];
    float blendZone = (cEnd - cStart) * 0.1;
    float blendStart = cEnd - blendZone;

    if (viewZ > blendStart && cascade < 3) {
        float nextVal     = sampleCascade(cascade + 1, offsetPos);
        float blendFactor = (viewZ - blendStart) / blendZone;
        shadowVal = mix(shadowVal, nextVal, blendFactor);
    }

    return shadowVal;
}

void main()
{
    vec3 N = normalize(fragNormal);
    vec3 sunDir = normalize(lightDirWorld.xyz);
    float NdotL = max(dot(N, sunDir), 0.0);

    // Shadow.
    float shadow = (shadowMapSize > 0.0) ? calcShadow(fragWorldPos, N) : 1.0;

    // Hemisphere ambient from UBO ambient color.
    // Bias sky/ground from the ambient color -- sky gets the full ambient, ground gets dimmer.
    vec3 skyAmb    = ambientColor.rgb * 2.0;
    vec3 groundAmb = ambientColor.rgb * 0.5;
    float hemiFactor = N.y * 0.5 + 0.5;
    vec3 ambient = mix(groundAmb, skyAmb, hemiFactor);

    // Sun light.
    vec3 sunLighting = lightColor.rgb * lightColor.a * NdotL * shadow;

    // Fill light (opposite direction, no shadow).
    float fillNdotL = max(dot(N, -sunDir), 0.0);
    vec3 fillLighting = fillColor.rgb * fillColor.a * fillNdotL;

    vec3 lighting = sunLighting + fillLighting + ambient;

    if (fragIsFloor > 0.5) {
        // Red matte floor with thin black grid lines
        vec2 tileUV = fragWorldPos.xz / 100.0;
        vec2 grid   = abs(fract(tileUV) - 0.5);
        float line  = min(grid.x, grid.y);
        float gridMask = smoothstep(0.005, 0.015, line);
        vec3 base = mix(vec3(0.02), vec3(0.70, 0.12, 0.10), gridMask);
        outColor = vec4(base * lighting, 1.0);
    } else {
        outColor = vec4(fragColor * lighting, 1.0);
    }
}
