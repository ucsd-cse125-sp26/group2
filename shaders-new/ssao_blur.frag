#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D aoTex;
layout(set = 2, binding = 1) uniform sampler2D depthTex;
layout(set = 2, binding = 2) uniform sampler2D normalTex;

layout(set = 3, binding = 0) uniform BlurParams {
    mat4 inverseViewProjection;
    vec2 inverseResolution;
    float radius;
    float blurRadius;
    float depthThreshold;
    float normalThreshold;
    float strength;
    float _pad0;
};

vec3 reconstructWorldPosition(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = inverseViewProjection * clip;
    return world.xyz / world.w;
}

void main()
{
    vec2 sceneUV = vec2(fragUV.x, 1.0 - fragUV.y);
    float centerDepth = texture(depthTex, sceneUV).r;
    vec4 centerNormalData = texture(normalTex, sceneUV);
    if (centerNormalData.a < 0.5 || centerDepth >= 1.0) {
        color = vec4(1.0);
        return;
    }

    vec3 centerPos = reconstructWorldPosition(sceneUV, centerDepth);
    vec3 centerNormal = normalize(centerNormalData.xyz * 2.0 - 1.0);
    float aoSum = 0.0;
    float weightSum = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUV = sceneUV + vec2(x, y) * blurRadius * inverseResolution;
            if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
                continue;

            float sampleDepth = texture(depthTex, sampleUV).r;
            vec4 sampleNormalData = texture(normalTex, sampleUV);
            if (sampleNormalData.a < 0.5 || sampleDepth >= 1.0)
                continue;

            vec3 samplePos = reconstructWorldPosition(sampleUV, sampleDepth);
            vec3 sampleNormal = normalize(sampleNormalData.xyz * 2.0 - 1.0);
            float positionWeight = 1.0 - smoothstep(0.0, max(radius * depthThreshold, 0.001), length(samplePos - centerPos));
            if (positionWeight <= 0.0)
                continue;

            float normalDot = dot(centerNormal, sampleNormal);
            if (normalDot < normalThreshold)
                continue;

            float normalWeight = smoothstep(normalThreshold, 1.0, normalDot);
            float spatialWeight = 1.0 / (1.0 + float(x * x + y * y));
            float weight = positionWeight * normalWeight * spatialWeight;
            aoSum += texture(aoTex, sampleUV).r * weight;
            weightSum += weight;
        }
    }

    float rawAo = texture(aoTex, sceneUV).r;
    float blurredAo = weightSum > 0.0 ? aoSum / weightSum : rawAo;
    float ao = mix(rawAo, blurredAo, clamp(strength, 0.0, 1.0));
    color = vec4(vec3(ao), 1.0);
}
