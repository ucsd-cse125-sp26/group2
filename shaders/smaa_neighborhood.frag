/// @file smaa_neighborhood.frag
/// @brief SMAA neighborhood blending pass.
/// Blends the original HDR scene color using the blend weights computed
/// by smaa_blend.frag.  Pairs with fullscreen.vert.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

/// @brief Original HDR scene color.
layout(set = 2, binding = 0) uniform sampler2D colorTex;
/// @brief RGBA8 blend weights from the blend weight pass.
layout(set = 2, binding = 1) uniform sampler2D blendTex;
/// @brief Blurred SSAO buffer (R8, 1.0 = no occlusion).
layout(set = 2, binding = 2) uniform sampler2D ssaoBuffer;

/// @brief Neighborhood blending parameters.
layout(set = 3, binding = 0) uniform NeighborParams {
    vec2  screenSize;
    float ssaoStrength; // 0 = no AO baked in, >0 = bake AO into output
    float ssaoPower;    // AO power curve (matches tonemap.frag).
};

void main()
{
    vec2 pixelSize = 1.0 / screenSize;

    // Blend weights stored in the current pixel (up and left directions)
    // and in the right / bottom neighbors (right and down directions).
    vec4 a;
    a.x = texture(blendTex, fragTexCoord + vec2( 1.0,  0.0) * pixelSize).a; // right neighbor's "left" weight
    a.y = texture(blendTex, fragTexCoord + vec2( 0.0,  1.0) * pixelSize).g; // bottom neighbor's "up" weight
    a.zw = texture(blendTex, fragTexCoord).rb; // current pixel: left (r) and up (b) weights

    // If there are no blend weights at all, pass through (still bake AO).
    if (dot(a, vec4(1.0)) < 1e-5) {
        vec4 c = texture(colorTex, fragTexCoord);
        // Alpha carries the weapon viewmodel mask (0 = weapon, 1 = scene).
        // Particle blend states preserve destination alpha (ZERO, ONE), so
        // only weapon pixels have alpha == 0.0.
        float sceneMask = c.a;
        c.rgb *= mix(1.0, pow(texture(ssaoBuffer, fragTexCoord).r, ssaoPower), ssaoStrength * sceneMask);
        outColor = c;
        return;
    }

    // Determine the dominant blending direction.
    // Horizontal blending uses left/right weights; vertical uses up/down.
    bool isHorizontal = max(a.x, a.z) > max(a.y, a.w);

    vec2 blendDir;
    vec2 blendWeight;

    if (isHorizontal) {
        blendDir    = vec2(pixelSize.x, 0.0);
        blendWeight = vec2(a.x, a.z); // (right, left)
    } else {
        blendDir    = vec2(0.0, pixelSize.y);
        blendWeight = vec2(a.y, a.w); // (down, up)
    }

    // Choose the side with the larger weight.
    float weight;
    vec2 offset;
    if (blendWeight.x > blendWeight.y) {
        weight = blendWeight.x;
        offset = blendDir;
    } else {
        weight = blendWeight.y;
        offset = -blendDir;
    }

    // Blend the current pixel with the neighbor in the dominant direction.
    // Bake SSAO into the colors so the temporal resolve stabilises the
    // AO-darkened image, preventing per-frame AO flicker from jitter.
    vec4 current  = texture(colorTex, fragTexCoord);
    vec4 neighbor = texture(colorTex, fragTexCoord + offset);

    // Skip SSAO for weapon pixels (alpha == 0.0).  Particle blend states
    // preserve destination alpha (ZERO, ONE), so scene alpha stays 1.0.
    float maskCur  = current.a;
    float maskNeig = neighbor.a;
    float aoCur  = mix(1.0, pow(texture(ssaoBuffer, fragTexCoord).r,          ssaoPower), ssaoStrength * maskCur);
    float aoNeig = mix(1.0, pow(texture(ssaoBuffer, fragTexCoord + offset).r, ssaoPower), ssaoStrength * maskNeig);
    current.rgb  *= aoCur;
    neighbor.rgb *= aoNeig;

    outColor = mix(current, neighbor, weight);
}
