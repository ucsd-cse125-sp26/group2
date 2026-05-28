#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D sceneTex;

layout(set = 3, binding = 0) uniform FxaaParams {
    vec2 inverseResolution;
    float enabled;
    float _pad1;
};

void main()
{
    vec2 sceneUV = vec2(fragUV.x, 1.0 - fragUV.y);

    if (enabled < 0.5) {
        color = texture(sceneTex, sceneUV);
        return;
    }

    const float spanMax = 8.0;
    const float reduceMul = 1.0 / 8.0;
    const float reduceMin = 1.0 / 128.0;
    const vec3 luma = vec3(0.299, 0.587, 0.114);

    vec3 rgbNW = texture(sceneTex, sceneUV + inverseResolution * vec2(-1.0, -1.0)).rgb;
    vec3 rgbNE = texture(sceneTex, sceneUV + inverseResolution * vec2(1.0, -1.0)).rgb;
    vec3 rgbSW = texture(sceneTex, sceneUV + inverseResolution * vec2(-1.0, 1.0)).rgb;
    vec3 rgbSE = texture(sceneTex, sceneUV + inverseResolution * vec2(1.0, 1.0)).rgb;
    vec4 rgbaM = texture(sceneTex, sceneUV);
    vec3 rgbM = rgbaM.rgb;

    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * reduceMul), reduceMin);
    float reciprocalDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * reciprocalDirMin, vec2(-spanMax), vec2(spanMax)) * inverseResolution;

    vec3 rgbA = 0.5 * (texture(sceneTex, sceneUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(sceneTex, sceneUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(sceneTex, sceneUV + dir * -0.5).rgb +
                                     texture(sceneTex, sceneUV + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);
    color = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, rgbaM.a);
}
