#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D depthTex;
layout(set = 2, binding = 1) uniform sampler2D normalTex;

layout(set = 3, binding = 0) uniform SsaoParams {
    mat4 inverseViewProjection;
    mat4 viewProjection;
    vec2 inverseResolution;
    float radius;
    float bias;
    float pixelRadiusScale;
    float depthJumpLimit;
    float normalDiffMin;
    float normalDiffMax;
    float hemisphereMin;
    float contactWeight;
    float mediumRadius;
    float mediumWeight;
    float intensity;
    float minAo;
    float maxAo;
    float _pad0;
};

const vec2 offsets[12] = vec2[](
    vec2( 1.0,  0.0),
    vec2(-1.0,  0.0),
    vec2( 0.0,  1.0),
    vec2( 0.0, -1.0),
    vec2( 0.7,  0.7),
    vec2(-0.7,  0.7),
    vec2( 0.7, -0.7),
    vec2(-0.7, -0.7),
    vec2( 1.6,  0.5),
    vec2(-1.6,  0.5),
    vec2( 0.5,  1.6),
    vec2( 0.5, -1.6)
);

vec3 reconstructWorldPosition(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = inverseViewProjection * clip;
    return world.xyz / world.w;
}

float sampleAoBand(vec2 sceneUV, float centerDepth, vec3 centerPos, vec3 centerNormal, float bandRadius)
{
    float pixelRadius = clamp(bandRadius * pixelRadiusScale, 1.0, 32.0);
    float safeNormalDiffMax = max(normalDiffMax, normalDiffMin + 0.001);
    float safeHemisphereMin = clamp(hemisphereMin, 0.0, 0.95);
    float occlusion = 0.0;

    for (int i = 0; i < 12; ++i) {
        vec2 sampleUV = sceneUV + offsets[i] * pixelRadius * inverseResolution;
        if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
            continue;

        float sampleDepth = texture(depthTex, sampleUV).r;
        if (sampleDepth >= 1.0 || abs(sampleDepth - centerDepth) > depthJumpLimit)
            continue;

        vec4 sampleNormalData = texture(normalTex, sampleUV);
        if (sampleNormalData.a < 0.5)
            continue;

        vec3 samplePos = reconstructWorldPosition(sampleUV, sampleDepth);
        vec3 delta = samplePos - centerPos;
        float dist = length(delta);
        if (dist <= max(bias, 0.001) || dist > bandRadius)
            continue;

        vec3 dir = delta / dist;
        float hemisphere = max(dot(centerNormal, dir), 0.0);
        if (hemisphere <= safeHemisphereMin)
            continue;

        vec3 sampleNormal = normalize(sampleNormalData.xyz * 2.0 - 1.0);
        float normalSimilarity = max(dot(centerNormal, sampleNormal), 0.0);
        float normalDiff = 1.0 - normalSimilarity;
        float creaseWeight = smoothstep(normalDiffMin, safeNormalDiffMax, normalDiff);
        if (creaseWeight <= 0.0)
            continue;

        float hemisphereWeight = smoothstep(safeHemisphereMin, 0.75, hemisphere);
        float distanceWeight = 1.0 - smoothstep(0.0, bandRadius, dist);
        occlusion += hemisphereWeight * creaseWeight * distanceWeight;
    }

    return occlusion / 12.0;
}

void main()
{
    vec2 sceneUV = vec2(fragUV.x, 1.0 - fragUV.y);
    vec4 normalData = texture(normalTex, sceneUV);
    float depth = texture(depthTex, sceneUV).r;
    if (normalData.a < 0.5 || depth >= 1.0) {
        color = vec4(1.0);
        return;
    }

    vec3 worldPos = reconstructWorldPosition(sceneUV, depth);
    vec3 normal = normalize(normalData.xyz * 2.0 - 1.0);
    float contactOcclusion = sampleAoBand(sceneUV, depth, worldPos, normal, radius);
    float mediumOcclusion = sampleAoBand(sceneUV, depth, worldPos, normal, mediumRadius);
    float occlusion = contactOcclusion * contactWeight + mediumOcclusion * mediumWeight;

    float safeMinAo = min(minAo, maxAo);
    float safeMaxAo = max(minAo, maxAo);
    float ao = 1.0 - intensity * occlusion;
    color = vec4(vec3(clamp(ao, safeMinAo, safeMaxAo)), 1.0);
}
