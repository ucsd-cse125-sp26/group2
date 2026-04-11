// bloom_downsample.frag — 13-tap downsample filter for bloom mip chain.
// Uses Karis average on the first pass to suppress fireflies.
// Paired with fullscreen.vert.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D srcTexture;

layout(set = 3, binding = 0) uniform DownsampleParams {
    vec2 srcResolution;
    float isFirstPass; // 1.0 for Karis average, 0.0 otherwise
    float _pad;
};

// Karis average: weighted average that suppresses bright outliers.
float karisWeight(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

void main()
{
    vec2 texelSize = 1.0 / srcResolution;
    vec2 uv = fragTexCoord;

    // 13-tap filter (Jimenez 2014, used in Call of Duty: Advanced Warfare).
    // Samples a 4×4 area with bilinear filtering for a smooth downsample.
    vec3 a = texture(srcTexture, uv + texelSize * vec2(-1, -1)).rgb;
    vec3 b = texture(srcTexture, uv + texelSize * vec2( 0, -1)).rgb;
    vec3 c = texture(srcTexture, uv + texelSize * vec2( 1, -1)).rgb;
    vec3 d = texture(srcTexture, uv + texelSize * vec2(-0.5, -0.5)).rgb;
    vec3 e = texture(srcTexture, uv + texelSize * vec2( 0.5, -0.5)).rgb;
    vec3 f = texture(srcTexture, uv + texelSize * vec2(-1,  0)).rgb;
    vec3 g = texture(srcTexture, uv).rgb;
    vec3 h = texture(srcTexture, uv + texelSize * vec2( 1,  0)).rgb;
    vec3 i = texture(srcTexture, uv + texelSize * vec2(-0.5, 0.5)).rgb;
    vec3 j = texture(srcTexture, uv + texelSize * vec2( 0.5, 0.5)).rgb;
    vec3 k = texture(srcTexture, uv + texelSize * vec2(-1,  1)).rgb;
    vec3 l = texture(srcTexture, uv + texelSize * vec2( 0,  1)).rgb;
    vec3 m = texture(srcTexture, uv + texelSize * vec2( 1,  1)).rgb;

    vec3 result;
    if (isFirstPass > 0.5) {
        // Karis average for first downsample to suppress fireflies.
        vec3 g0 = (a + b + f + g) * 0.25;
        vec3 g1 = (b + c + g + h) * 0.25;
        vec3 g2 = (f + g + k + l) * 0.25;
        vec3 g3 = (g + h + l + m) * 0.25;
        vec3 g4 = (d + e + i + j) * 0.25;

        float w0 = karisWeight(g0);
        float w1 = karisWeight(g1);
        float w2 = karisWeight(g2);
        float w3 = karisWeight(g3);
        float w4 = karisWeight(g4);
        float wSum = w0 + w1 + w2 + w3 + w4;

        result = (g0*w0 + g1*w1 + g2*w2 + g3*w3 + g4*w4) / wSum;
    } else {
        // Standard 13-tap downsample.
        result = g * 0.125;
        result += (d + e + i + j) * 0.125;
        result += (a + c + k + m) * 0.03125;
        result += (b + f + h + l) * 0.0625;
    }

    outColor = vec4(result, 1.0);
}
