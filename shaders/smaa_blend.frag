/// @file smaa_blend.frag
/// @brief SMAA blending weight calculation pass.
/// Ported from the SMAA reference implementation by Jorge Jimenez et al. (MIT License).
/// Pairs with fullscreen.vert.
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

/// @brief RG8 edge detection output from smaa_edge.frag.
layout(set = 2, binding = 0) uniform sampler2D edgeTex;
/// @brief RG8 160x560 SMAA area lookup texture.
layout(set = 2, binding = 1) uniform sampler2D areaTex;
/// @brief R8 66x33 SMAA search lookup texture.
layout(set = 2, binding = 2) uniform sampler2D searchTex;

/// @brief Blend weight calculation parameters.
layout(set = 3, binding = 0) uniform BlendParams {
    vec4 screenSizeAndSubpixel; // xy = screenSize, z = subPixelIndex (0.0 or 1.0), w = pad
};

// ---------------------------------------------------------------------------
// SMAA constants
// ---------------------------------------------------------------------------
#define SMAA_THRESHOLD 0.1
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_AREATEX_MAX_DISTANCE 16.0
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / vec2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE vec2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE vec2(64.0, 16.0)
#define SMAA_CORNER_ROUNDING 25
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
vec2 pixelSize;

/// Decode the edge information stored in a bilinear-filtered fetch of the
/// edge texture.  Returns 0 or 1 per channel.
vec2 decodeEdge(vec2 e)
{
    e = e * (255.0 / 127.0);  // undo hardware bilinear for RG8
    return step(0.5, e);      // binarise
}

// ---------------------------------------------------------------------------
// Search functions
// ---------------------------------------------------------------------------

/// Search for the end of an edge along the horizontal (left or right).
/// @param dir  -1.0 for left, +1.0 for right.
float searchXLeft(vec2 texcoord)
{
    // Bias the texcoord so bilinear sampling fetches two horizontal pixels:
    //   offset = -0.25 * pixelSize.x  =>  filter straddles current + left
    vec2 e = vec2(0.0, 1.0);
    float end = texcoord.x;

    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        texcoord.x -= 2.0 * pixelSize.x;
        e = texture(edgeTex, texcoord).rg;
        // Keep going while there is an edge above (g channel) and no
        // perpendicular edge (r channel).
        if (e.g < 0.8281 || e.r > 0.0) break;
    }

    // Sub-pixel refinement via bilinear acceleration with search texture.
    float offset = -(255.0 / 127.0) * texture(searchTex,
        vec2(e.g * 0.5, 0.0 + e.r * 0.5) * SMAA_SEARCHTEX_PACKED_SIZE / SMAA_SEARCHTEX_SIZE
    ).r + 3.25;
    return texcoord.x + offset * pixelSize.x;
}

float searchXRight(vec2 texcoord)
{
    vec2 e = vec2(0.0, 1.0);

    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        texcoord.x += 2.0 * pixelSize.x;
        e = texture(edgeTex, texcoord).rg;
        if (e.g < 0.8281 || e.r > 0.0) break;
    }

    float offset = -(255.0 / 127.0) * texture(searchTex,
        vec2(e.g * 0.5, 0.5 + e.r * 0.5) * SMAA_SEARCHTEX_PACKED_SIZE / SMAA_SEARCHTEX_SIZE
    ).r + 3.25;
    return texcoord.x - offset * pixelSize.x;
}

float searchYUp(vec2 texcoord)
{
    vec2 e = vec2(1.0, 0.0);

    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        texcoord.y -= 2.0 * pixelSize.y;
        e = texture(edgeTex, texcoord).rg;
        if (e.r < 0.8281 || e.g > 0.0) break;
    }

    float offset = -(255.0 / 127.0) * texture(searchTex,
        vec2(e.r * 0.5, 0.0 + e.g * 0.5) * SMAA_SEARCHTEX_PACKED_SIZE / SMAA_SEARCHTEX_SIZE
    ).r + 3.25;
    return texcoord.y + offset * pixelSize.y;
}

float searchYDown(vec2 texcoord)
{
    vec2 e = vec2(1.0, 0.0);

    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        texcoord.y += 2.0 * pixelSize.y;
        e = texture(edgeTex, texcoord).rg;
        if (e.r < 0.8281 || e.g > 0.0) break;
    }

    float offset = -(255.0 / 127.0) * texture(searchTex,
        vec2(e.r * 0.5, 0.5 + e.g * 0.5) * SMAA_SEARCHTEX_PACKED_SIZE / SMAA_SEARCHTEX_SIZE
    ).r + 3.25;
    return texcoord.y - offset * pixelSize.y;
}

// ---------------------------------------------------------------------------
// Diagonal search
// ---------------------------------------------------------------------------
vec2 searchDiag1(vec2 texcoord, vec2 dir)
{
    vec2 e = vec2(0.0);
    float end = 0.0;
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS_DIAG; i++) {
        texcoord += dir * pixelSize;
        e = texture(edgeTex, texcoord).rg;
        if (dot(e, vec2(1.0)) < 1.5) break;
        end = float(i);
    }
    return vec2(end, e.g);
}

vec2 searchDiag2(vec2 texcoord, vec2 dir)
{
    vec2 e = vec2(0.0);
    float end = 0.0;
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS_DIAG; i++) {
        texcoord += vec2(dir.x, -dir.y) * pixelSize;
        e = texture(edgeTex, texcoord).rg;
        if (dot(e, vec2(1.0)) < 1.5) break;
        end = float(i);
    }
    return vec2(end, e.g);
}

// ---------------------------------------------------------------------------
// Area texture lookup
// ---------------------------------------------------------------------------

/// Fetch blending area weights from the area texture.
/// @param dist   distances (d1, d2) to the edge endpoints.
/// @param e1e2   crossing edge flags at the two endpoints.
/// @param offset sub-pixel offset (0.0 for frame 0, 1.0 for frame 1 in T2x).
vec2 areaLookup(vec2 dist, float e1, float e2, float offset)
{
    // Area texture is organised as a grid of 5x5 cells.
    // Each cell stores weights for a particular (e1, e2) pattern.
    // Rows cycle through 7 sub-pixel offsets.
    vec2 texcoord = SMAA_AREATEX_MAX_DISTANCE * round(4.0 * vec2(e1, e2)) + dist;

    // Scale to [0,1] range within the area texture, adding sub-pixel row offset.
    texcoord = SMAA_AREATEX_PIXEL_SIZE * (texcoord + 0.5);
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;

    return texture(areaTex, texcoord).rg;
}

vec2 areaDiagLookup(vec2 dist, float e1, float e2, float offset)
{
    vec2 texcoord;
    texcoord.x = SMAA_AREATEX_MAX_DISTANCE * e1 + dist.x;
    // Diagonal area is stored in the lower half of the area texture.
    texcoord.y = SMAA_AREATEX_MAX_DISTANCE * e2 + dist.y;

    texcoord = SMAA_AREATEX_PIXEL_SIZE * (texcoord + 0.5);
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;
    // Diagonal patterns start at row offset 0.5 in texture space.
    texcoord.y += 0.5;

    return texture(areaTex, texcoord).rg;
}

// ---------------------------------------------------------------------------
// Corner detection helpers
// ---------------------------------------------------------------------------
void detectHorizontalCornerPattern(inout vec2 weights, vec2 texcoord, vec2 d)
{
    vec4 coords = vec4(texcoord.x - d.x * pixelSize.x, texcoord.x + d.y * pixelSize.x,
                       texcoord.y - pixelSize.y, texcoord.y + pixelSize.y);

    vec2 e;
    e.r = texture(edgeTex, vec2(coords.x, coords.z)).r;
    e.g = texture(edgeTex, vec2(coords.y, coords.w)).r;

    // Reduce weights at corners to prevent over-blurring.
    weights *= clamp(vec2(1.0) - vec2(e.r, e.g) * SMAA_CORNER_ROUNDING_NORM, 0.0, 1.0);
}

void detectVerticalCornerPattern(inout vec2 weights, vec2 texcoord, vec2 d)
{
    vec4 coords = vec4(texcoord.x - pixelSize.x, texcoord.x + pixelSize.x,
                       texcoord.y - d.x * pixelSize.y, texcoord.y + d.y * pixelSize.y);

    vec2 e;
    e.r = texture(edgeTex, vec2(coords.x, coords.z)).g;
    e.g = texture(edgeTex, vec2(coords.y, coords.w)).g;

    weights *= clamp(vec2(1.0) - vec2(e.r, e.g) * SMAA_CORNER_ROUNDING_NORM, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void main()
{
    pixelSize = 1.0 / screenSizeAndSubpixel.xy;
    float subPixelIndex = screenSizeAndSubpixel.z;
    vec4 weights = vec4(0.0);

    vec2 e = texture(edgeTex, fragTexCoord).rg;

    // Early out if there are no edges here.
    if (dot(e, vec2(1.0)) == 0.0) {
        outColor = vec4(0.0);
        return;
    }

    // -------------------------------------------------------------------
    // Diagonal processing
    // -------------------------------------------------------------------
    if (e.r > 0.0 && e.g > 0.0) {
        // Diagonal search: try -1,-1 and +1,+1 (main diagonal)
        vec2 d1 = searchDiag1(fragTexCoord, vec2(-1.0, -1.0));
        float d1len = d1.x;
        vec2 d2 = searchDiag1(fragTexCoord, vec2(1.0, 1.0));
        float d2len = d2.x;

        if (d1len + d2len > 2.0) {
            // We found a diagonal pattern.
            vec2 dist = vec2(d1len, d2len);
            vec2 areaWeights = areaDiagLookup(dist,
                texture(edgeTex, fragTexCoord - (d1len + 0.5) * vec2(1.0, 1.0) * pixelSize).r,
                d2.y,
                subPixelIndex);
            weights.rg = areaWeights;
        }

        // Anti-diagonal: try -1,+1 and +1,-1
        d1 = searchDiag2(fragTexCoord, vec2(-1.0, -1.0));
        d1len = d1.x;
        d2 = searchDiag2(fragTexCoord, vec2(1.0, 1.0));
        d2len = d2.x;

        if (d1len + d2len > 2.0) {
            vec2 dist = vec2(d1len, d2len);
            vec2 areaWeights = areaDiagLookup(dist,
                texture(edgeTex, fragTexCoord + vec2(-d1len - 0.5, d1len + 0.5) * pixelSize).r,
                d2.y,
                subPixelIndex);
            weights.ba = areaWeights;
        }
    }

    // -------------------------------------------------------------------
    // Horizontal edge processing
    // -------------------------------------------------------------------
    if (e.r > 0.0) {
        // Search for edge endpoints.
        float left  = searchXLeft(fragTexCoord);
        float right = searchXRight(fragTexCoord);

        // Distances in pixels.
        vec2 d = vec2(left, right);
        d = abs(d / pixelSize.x - fragTexCoord.x / pixelSize.x);

        // Fetch crossing edges at the two endpoints.
        float e1 = texture(edgeTex, vec2(left  + 0.25 * pixelSize.x, fragTexCoord.y - pixelSize.y)).r;
        float e2 = texture(edgeTex, vec2(right - 0.25 * pixelSize.x, fragTexCoord.y - pixelSize.y)).r;

        // Area texture lookup.
        weights.rg = areaLookup(sqrt(d), e1, e2, subPixelIndex);

        // Corner detection.
        detectHorizontalCornerPattern(weights.rg, fragTexCoord, d);
    }

    // -------------------------------------------------------------------
    // Vertical edge processing
    // -------------------------------------------------------------------
    if (e.g > 0.0) {
        float up   = searchYUp(fragTexCoord);
        float down = searchYDown(fragTexCoord);

        vec2 d = vec2(up, down);
        d = abs(d / pixelSize.y - fragTexCoord.y / pixelSize.y);

        float e1 = texture(edgeTex, vec2(fragTexCoord.x - pixelSize.x, up   + 0.25 * pixelSize.y)).g;
        float e2 = texture(edgeTex, vec2(fragTexCoord.x - pixelSize.x, down - 0.25 * pixelSize.y)).g;

        weights.ba = areaLookup(sqrt(d), e1, e2, subPixelIndex);

        detectVerticalCornerPattern(weights.ba, fragTexCoord, d);
    }

    outColor = weights;
}
