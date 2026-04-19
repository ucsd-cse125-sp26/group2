/// @file smaa_edge.frag
/// @brief SMAA luma-based edge detection pass.
/// Pairs with fullscreen.vert.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

/// @brief HDR scene color buffer.
layout(set = 2, binding = 0) uniform sampler2D hdrBuffer;
/// @brief Blurred SSAO buffer (R8, 1.0 = no occlusion).
layout(set = 2, binding = 1) uniform sampler2D ssaoBuffer;

/// @brief Edge detection parameters.
layout(set = 3, binding = 0) uniform EdgeParams {
    vec2  screenSize;
    float threshold;    // e.g. 0.1
    float ssaoStrength; // 0 = ignore AO, >0 = factor AO into luma
    float ssaoPower;    // AO power curve
};

/// Compute perceptual luma from linear RGB.
float luma(vec3 rgb)
{
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

/// Sample HDR color with SSAO pre-applied, then compute luma.
float sampleLuma(vec2 uv)
{
    vec3 c = texture(hdrBuffer, uv).rgb;
    float ao = pow(texture(ssaoBuffer, uv).r, ssaoPower);
    c *= mix(1.0, ao, ssaoStrength);
    return luma(c);
}

void main()
{
    vec2 pixelSize = 1.0 / screenSize;

    // Sample luma (with AO baked in) at current pixel and four neighbors.
    float L      = sampleLuma(fragTexCoord);
    float Lleft  = sampleLuma(fragTexCoord + vec2(-1.0,  0.0) * pixelSize);
    float Ltop   = sampleLuma(fragTexCoord + vec2( 0.0, -1.0) * pixelSize);
    float Lright = sampleLuma(fragTexCoord + vec2( 1.0,  0.0) * pixelSize);
    float Lbot   = sampleLuma(fragTexCoord + vec2( 0.0,  1.0) * pixelSize);

    // Deltas against left and top neighbors.
    vec2 delta = abs(vec2(L - Lleft, L - Ltop));

    // Local contrast adaptation: suppress edges in already-noisy areas.
    float maxDelta = max(max(abs(L - Lright), abs(L - Lbot)), max(delta.x, delta.y));

    // An edge is flagged when the delta exceeds the threshold AND is at
    // least half the local maximum contrast (avoids false positives in
    // high-frequency detail).
    vec2 edges = step(vec2(threshold), delta) * step(0.5 * maxDelta, delta);

    outColor = vec4(edges.x, edges.y, 0.0, 0.0);
}
