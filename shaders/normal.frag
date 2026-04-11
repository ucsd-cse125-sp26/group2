// normal.frag — Lit scene geometry with shadows, hemisphere ambient.
// ALL lighting parameters come from the UBO (driven by ImGui sliders).
#version 450

layout(location = 0) in  vec3       fragColor;
layout(location = 1) in  vec3       fragWorldPos;
layout(location = 2) in  float      fragIsFloor;
layout(location = 3) flat in vec3   fragNormal;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2DShadow shadowMap;

// All lighting + shadow data from C++ (matches ShadowDataFragUBO).
layout(set = 3, binding = 0) uniform SceneShadowData
{
    mat4  lightVP;
    float shadowBias;
    float shadowNormalBias;
    float shadowMapSize;
    float _pad;
    vec4  lightDirWorld;   // xyz = direction TO sun
    vec4  lightColor;      // rgb = sun color, a = sun intensity
    vec4  ambientColor;    // rgb = ambient color
    vec4  fillColor;       // rgb = fill color, a = fill intensity
};

// ── Shadow sampling (3x3 PCF with normal-offset bias) ─────────────────────
float calcShadow(vec3 worldPos, vec3 N)
{
    vec3 lightDir = normalize(lightDirWorld.xyz);
    float NdotL = dot(N, lightDir);
    float normalOffsetScale = shadowNormalBias * (1.0 - NdotL);
    vec3 offsetPos = worldPos + N * normalOffsetScale;

    vec4 lightClip = lightVP * vec4(offsetPos, 1.0);
    vec3 ndc = lightClip.xyz / lightClip.w;
    vec2 shadowUV = ndc.xy * 0.5 + 0.5;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;

    float currentDepth = ndc.z - shadowBias;
    float texelSize = 1.0 / shadowMapSize;
    float shadow = 0.0;

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
    vec3 sunDir = normalize(lightDirWorld.xyz);
    float NdotL = max(dot(N, sunDir), 0.0);

    // Shadow.
    float shadow = (shadowMapSize > 0.0) ? calcShadow(fragWorldPos, N) : 1.0;

    // Hemisphere ambient from UBO ambient color.
    // Bias sky/ground from the ambient color — sky gets the full ambient, ground gets dimmer.
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
        // ── Red matte floor with thin black grid lines ─────────────────────
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
