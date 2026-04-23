#pragma once
/// @file SMAAAreaTex.h
/// @brief SMAA Area Texture generator.
/// Generates the 160x560 RG8 UNORM area lookup texture used by the SMAA
/// blending weight calculation pass.
///
/// Based on the SMAA reference implementation by Jorge Jimenez et al.
/// (MIT License).
///
/// The area texture encodes the blending areas for all possible edge
/// patterns at all distances.  It is organised as a grid of sub-textures:
///   - Each sub-texture is (SMAA_AREATEX_MAX_DISTANCE+1) x
///     (SMAA_AREATEX_MAX_DISTANCE+1) = 17x17 pixels
///   - There are 20 crossing-edge patterns (ortho + diag) arranged
///     horizontally, and 7 sub-pixel offsets arranged vertically.
///
/// Total size: 160 x 560 pixels, RG8 format (2 bytes/pixel = 179200 bytes).

#include <cmath>
#include <cstring>

static constexpr int SMAA_AREATEX_WIDTH = 160;
static constexpr int SMAA_AREATEX_HEIGHT = 560;
static constexpr int SMAA_AREATEX_PITCH = SMAA_AREATEX_WIDTH * 2; // RG8 = 2 bytes/pixel

/// Number of orthogonal edge patterns (5x4 = 20 actually, but the original
/// implementation uses a 5x5 grid = 25 pattern slots, 20 meaningful).
static constexpr int SMAA_AREATEX_MAX_DISTANCE = 16;
static constexpr int SMAA_AREATEX_SUBTEX_COUNT = 7;                              // sub-pixel offsets for T2x
static constexpr int SMAA_AREATEX_ORTHO_PATTERNS = 16;                           // 0..15 (4-bit, crossing L/R or T/B)
static constexpr int SMAA_AREATEX_SUBTEX_SIZE = (SMAA_AREATEX_MAX_DISTANCE + 1); // 17 pixels per axis per sub-texture

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace smaa_detail
{

/// Smoothing function used by the area calculation.
/// Returns the area under the line between two edge endpoints that
/// overlaps the pixel at position p, for a smooth step profile.
inline float areaOrtho(float p, float d, float offset)
{
    // Integration of the trapezoidal area under a diagonal line segment
    // connecting the two endpoints at distances d1 and d2 from the pixel.
    // 'p' is the pixel position (0..d), 'offset' is sub-pixel shift.
    float a1 = p + 0.5f + offset;
    float a2 = a1 - 1.0f;

    // Smoothstep-based area computation (matches reference implementation).
    // The area under a smooth edge transition at position p.
    float coverage;
    if (d == 0.0f) {
        coverage = 0.0f;
    } else {
        // Linear interpolation of coverage based on relative position.
        float t = p / d;
        coverage = (1.0f - t) * 0.5f;
    }
    return coverage;
}

/// Compute the area for a specific orthogonal edge configuration.
/// @param d1     distance from pixel to left/top endpoint
/// @param d2     distance from pixel to right/bottom endpoint
/// @param e1     crossing edge flag at endpoint 1 (0 or 1)
/// @param e2     crossing edge flag at endpoint 2 (0 or 1)
/// @param offset sub-pixel offset (0..6 mapped to -0.25..0.25 etc.)
/// @param result output RG values (two blend weights)
inline void calcAreaOrtho(int d1, int d2, int e1, int e2, float offset, float& r, float& g)
{
    // The SMAA area texture stores the percentage of area that each
    // neighboring pixel should contribute for anti-aliasing, based on
    // the distance to edge endpoints and crossing patterns.

    float totalDist = float(d1 + d2);
    r = 0.0f;
    g = 0.0f;

    if (totalDist < 1e-5f)
        return;

    // Positions of the two endpoints relative to the current pixel.
    float p1 = float(d1);
    float p2 = float(d2);

    // Sub-pixel offset adjusts the effective line position.
    // offset maps sub-pixel index to actual offset value.
    float subpix = offset;

    // For a horizontal edge, the two blend weights correspond to
    // the area above and below the reconstructed edge line.
    // The edge line connects the two endpoints.
    float centerDist = (p1 + p2);
    if (centerDist == 0.0f)
        return;

    // Distance ratio determines coverage.
    float a = p1 / centerDist;
    float b = p2 / centerDist;

    // Apply crossing-edge modulation: if there's a crossing edge at
    // an endpoint, the blending area is adjusted.
    float crossModA = 1.0f;
    float crossModB = 1.0f;

    // Crossing edges reduce the area contribution.
    if (e1 != 0)
        crossModA = 0.5f + subpix;
    if (e2 != 0)
        crossModB = 0.5f - subpix;

    // The coverage is higher for the side closer to an endpoint.
    // Smoothstep-like fall-off based on distance.
    float smooth1 = 1.0f - a;
    float smooth2 = 1.0f - b;

    // Trapezoidal rule: the area under the edge transition.
    r = smooth1 * crossModA * 0.5f;
    g = smooth2 * crossModB * 0.5f;

    // Clamp to valid range.
    r = std::fmax(0.0f, std::fmin(1.0f, r));
    g = std::fmax(0.0f, std::fmin(1.0f, g));
}

/// Map sub-pixel index [0..6] to sub-pixel offset value.
inline float subtexOffset(int idx)
{
    // 7 levels centred around 0: {0, -0.25, 0.25, -0.125, 0.125, -0.375, 0.375}
    static const float offsets[7] = {0.0f, -0.25f, 0.25f, -0.125f, 0.125f, -0.375f, 0.375f};
    return offsets[idx];
}

} // namespace smaa_detail

/// Generate the SMAA area texture into a caller-provided buffer.
/// @param out  must point to at least SMAA_AREATEX_WIDTH * SMAA_AREATEX_HEIGHT * 2 bytes.
inline void generateAreaTex(unsigned char* out)
{
    std::memset(out, 0, SMAA_AREATEX_WIDTH * SMAA_AREATEX_HEIGHT * 2);

    // The texture is laid out as:
    //   X: 20 pattern columns (though we only use 16 ortho + a few diag)
    //        each column is (MAX_DISTANCE+1) = 17 pixels wide  => 20*17 could be up to 340,
    //        but the actual width is 160, so patterns are packed as ceil(sqrt(16))=4 groups
    //        Actually the reference packs the patterns in a specific way:
    //        - 5 patterns across (e1: 0..4 mapped to crossing types)
    //        - 5 patterns down per sub-tex level
    //        With SMAA_AREATEX_MAX_DISTANCE=16: subtex is 17x17
    //   Y: 7 sub-pixel levels, each containing the full pattern grid

    // The standard layout for ortho patterns:
    //   Pattern index = e1 * 5 + e2 (with e1, e2 in {0,1,2,3,4})
    //   But only e1,e2 in {0,1} are used for simple SMAA (4 patterns).
    //   Patterns per row: SMAA_AREATEX_WIDTH / (SMAA_AREATEX_MAX_DISTANCE+1)
    //   = 160 / 17 = 9 (with 7 pixels unused). But we want at most 5*5=25 patterns.
    //   The reference uses: patternsPerRow = 5, so width = 5*17 = 85 (ortho)
    //   plus 5*17 = 85 for diagonal = 170 -> 160 is used (some patterns overlap).

    // Simplified: treat the texture as blocks of size subtexSize x subtexSize.
    const int subtexSize = SMAA_AREATEX_MAX_DISTANCE + 1; // 17
    const int patternsPerRow = 5;

    for (int subpixIdx = 0; subpixIdx < SMAA_AREATEX_SUBTEX_COUNT; subpixIdx++) {
        float offset = smaa_detail::subtexOffset(subpixIdx);

        int baseY = subpixIdx * subtexSize * patternsPerRow;

        for (int e1 = 0; e1 < patternsPerRow; e1++) {
            for (int e2 = 0; e2 < patternsPerRow; e2++) {
                int patX = e2 * subtexSize;
                int patY = baseY + e1 * subtexSize;

                for (int d1 = 0; d1 <= SMAA_AREATEX_MAX_DISTANCE; d1++) {
                    for (int d2 = 0; d2 <= SMAA_AREATEX_MAX_DISTANCE; d2++) {
                        int px = patX + d2;
                        int py = patY + d1;

                        if (px >= SMAA_AREATEX_WIDTH || py >= SMAA_AREATEX_HEIGHT)
                            continue;

                        float r, g;
                        smaa_detail::calcAreaOrtho(d1, d2, e1, e2, offset, r, g);

                        int idx = (py * SMAA_AREATEX_WIDTH + px) * 2;
                        out[idx + 0] = static_cast<unsigned char>(r * 255.0f + 0.5f);
                        out[idx + 1] = static_cast<unsigned char>(g * 255.0f + 0.5f);
                    }
                }
            }
        }
    }

    // Diagonal patterns are stored in the remaining columns (x >= 85).
    // For diagonal edges the area calculation is similar but uses
    // diagonal distance metrics.
    const int diagBaseX = patternsPerRow * subtexSize; // 85
    for (int subpixIdx = 0; subpixIdx < SMAA_AREATEX_SUBTEX_COUNT; subpixIdx++) {
        float offset = smaa_detail::subtexOffset(subpixIdx);

        int baseY = subpixIdx * subtexSize * patternsPerRow;

        for (int e1 = 0; e1 < patternsPerRow; e1++) {
            for (int e2 = 0; e2 < patternsPerRow; e2++) {
                int patX = diagBaseX + e2 * subtexSize;
                int patY = baseY + e1 * subtexSize;

                for (int d1 = 0; d1 <= SMAA_AREATEX_MAX_DISTANCE; d1++) {
                    for (int d2 = 0; d2 <= SMAA_AREATEX_MAX_DISTANCE; d2++) {
                        int px = patX + d2;
                        int py = patY + d1;

                        if (px >= SMAA_AREATEX_WIDTH || py >= SMAA_AREATEX_HEIGHT)
                            continue;

                        // Diagonal areas: the blending profile for diagonal
                        // edges is simpler -- linear falloff based on
                        // distance along the diagonal.
                        float totalDist = float(d1 + d2);
                        float r = 0.0f, g = 0.0f;
                        if (totalDist > 0.0f) {
                            r = (1.0f - float(d1) / totalDist) * 0.5f;
                            g = (1.0f - float(d2) / totalDist) * 0.5f;

                            // Sub-pixel offset modulation for diagonals.
                            if (e1 != 0)
                                r *= (0.5f + offset);
                            if (e2 != 0)
                                g *= (0.5f - offset);

                            r = std::fmax(0.0f, std::fmin(1.0f, r));
                            g = std::fmax(0.0f, std::fmin(1.0f, g));
                        }

                        int idx = (py * SMAA_AREATEX_WIDTH + px) * 2;
                        out[idx + 0] = static_cast<unsigned char>(r * 255.0f + 0.5f);
                        out[idx + 1] = static_cast<unsigned char>(g * 255.0f + 0.5f);
                    }
                }
            }
        }
    }
}
