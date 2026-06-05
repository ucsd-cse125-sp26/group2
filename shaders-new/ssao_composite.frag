#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D sceneTex;
layout(set = 2, binding = 1) uniform sampler2D rawAoTex;
layout(set = 2, binding = 2) uniform sampler2D blurredAoTex;

layout(set = 3, binding = 0) uniform CompositeParams {
    float strength;
    float power;
    uint blurEnabled;
    int debugView;
};

void main()
{
    vec2 sceneUV = vec2(fragUV.x, 1.0 - fragUV.y);
    vec4 scene = texture(sceneTex, sceneUV);
    float rawAo = texture(rawAoTex, sceneUV).r;
    float ao = rawAo;
    if (blurEnabled != 0)
        ao = texture(blurredAoTex, sceneUV).r;
    float aoPower = max(power, 0.001);
    float curvedAo = pow(clamp(ao, 0.0, 1.0), aoPower);
    float aoStrength = max(strength, 0.0);

    if (debugView == 1) {
        color = vec4(vec3(rawAo), 1.0);
    } else {
        float aoFactor = clamp(mix(1.0, curvedAo, aoStrength), 0.0, 1.0);
        color = vec4(scene.rgb * aoFactor, scene.a);
    }
}
