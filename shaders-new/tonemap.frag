#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D sceneTex;

layout(set = 3, binding = 0) uniform TonemapParams {
    float exposure;
    float whitePoint;
    float _pad0;
    float _pad1;
};

void main()
{
    vec2 sceneUV = vec2(fragUV.x, 1.0 - fragUV.y);
    vec4 hdr = texture(sceneTex, sceneUV);
    vec3 linearColor = max(hdr.rgb, vec3(0.0));

    const float epsilon = 0.0001;
    const vec3 luminanceWeights = vec3(0.2126, 0.7152, 0.0722);

    vec3 exposedColor = linearColor * max(exposure, 0.0);
    float luminance = dot(exposedColor, luminanceWeights);
    float safeWhitePoint = max(whitePoint, epsilon);
    float whitePointSq = safeWhitePoint * safeWhitePoint;
    float mappedLuminance = (luminance * (1.0 + luminance / whitePointSq)) / (1.0 + luminance);
    float scale = mappedLuminance / max(luminance, epsilon);
    linearColor = exposedColor * scale;

    color = vec4(pow(max(linearColor, vec3(0.0)), vec3(1.0 / 2.2)), hdr.a);
}
