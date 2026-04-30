#pragma once
/// @file SMAASearchTex.h
/// @brief SMAA Search Texture generator.
/// Generates the 66x33 R8 UNORM search lookup texture used by the SMAA
/// blending weight calculation pass.
///
/// Based on the SMAA reference implementation by Jorge Jimenez et al.
/// (MIT License).
///
/// The search texture is used for bilinear-filtering acceleration of edge
/// endpoint detection.  Each texel encodes whether a particular bilinear
/// combination of two horizontally/vertically adjacent edge pixels indicates
/// "continue searching" or "endpoint found".
///
/// The key insight is that bilinear filtering of two edge pixels yields a
/// specific set of values depending on the (left, right) or (top, bottom)
/// edge configuration.  The search texture maps each bilinear result to a
/// sub-pixel-accurate endpoint position.
///
/// Total size: 66 x 33 pixels, R8 format (2178 bytes).

#include <cmath>
#include <cstring>

static constexpr int SMAA_SEARCHTEX_WIDTH = 66;
static constexpr int SMAA_SEARCHTEX_HEIGHT = 33;
static constexpr int SMAA_SEARCHTEX_PITCH = SMAA_SEARCHTEX_WIDTH; // R8 = 1 byte/pixel

/// Packed texture dimensions used during bilinear sampling.
static constexpr int SMAA_SEARCHTEX_PACKED_WIDTH = 64;
static constexpr int SMAA_SEARCHTEX_PACKED_HEIGHT = 16;

// Internal helpers
namespace smaa_search_detail
{

/// Encode two edge values (e1, e2 in {0,1}) and their bilinear position
/// into a search texture value.
///
/// When the GPU bilinear-samples two adjacent edge texels, the result
/// encodes both texels' values.  The search texture pre-computes for every
/// possible bilinear result:
///   - 0 (0.0):   no edge found => stop searching
///   - 127 (0.5): partial match => keep searching
///   - 255 (1.0): exact match   => endpoint found
///
/// The texture is indexed by:
///   u = bilinear(e.g) * 0.5  (horizontal: edge green channel)
///   v = e.r * 0.5            (perpendicular crossing edge)
///
/// For the left/up search direction, uvs are offset differently than for
/// right/down.

/// Compute the search texture value for a given edge configuration.
/// @param e1   left/top edge pixel value (0 or 1)
/// @param e2   right/bottom edge pixel value (0 or 1)
/// @param bias bilinear interpolation bias (0.0 to 1.0)
/// @return     search result: 0=stop, 127=continue, 255=found
inline unsigned char calcSearchValue(int e1, int e2, float bias)
{
    // Bilinear interpolation result of two adjacent edge values.
    float bilinear = float(e1) * (1.0f - bias) + float(e2) * bias;

    // If both edges are active (bilinear ~= 1.0), the search should continue.
    // If only one is active (bilinear ~= 0.5), we've found an endpoint.
    // If neither is active (bilinear ~= 0.0), there's no edge -- stop.

    if (bilinear < 0.25f) {
        return 0;   // No edge: stop
    } else if (bilinear < 0.75f) {
        return 127; // Endpoint found: return fractional offset
    } else {
        return 255; // Edge continues: keep searching
    }
}

} // namespace smaa_search_detail

/// Generate the SMAA search texture into a caller-provided buffer.
/// @param out  must point to at least SMAA_SEARCHTEX_WIDTH * SMAA_SEARCHTEX_HEIGHT bytes.
inline void generateSearchTex(unsigned char* out)
{
    std::memset(out, 0, SMAA_SEARCHTEX_WIDTH * SMAA_SEARCHTEX_HEIGHT);

    // The search texture is organised as two halves:
    //   Top half  (y = 0..15):  left/up search direction
    //   Bottom half (y = 16..32): right/down search direction
    //
    // Within each half:
    //   x axis (0..63): bilinear result of adjacent edge pixel pair, quantised
    //   y axis (0..15): crossing edge flag (0 or 1) and sub-type
    //
    // The extra 2 columns (64, 65) are padding for safe bilinear filtering.

    // For each possible combination of two adjacent edge texels:
    for (int e1 = 0; e1 < 2; e1++) {     // Current pixel edge
        for (int e2 = 0; e2 < 2; e2++) { // Adjacent pixel edge
            // Horizontal search patterns:
            // The bilinear filter result when sampling between e1 and e2
            // depends on the sub-pixel offset.

            // Generate entries for the search direction (left/up).
            for (int step = 0; step < SMAA_SEARCHTEX_PACKED_WIDTH; step++) {
                float bias = float(step) / float(SMAA_SEARCHTEX_PACKED_WIDTH - 1);

                unsigned char val = smaa_search_detail::calcSearchValue(e1, e2, bias);

                // Left/up half of texture.
                int x = step;
                int y = e1 * 8 + e2 * 4;

                // Fill a 4-row band for this pattern (provides filtering margin).
                for (int dy = 0; dy < 4; dy++) {
                    int py = y + dy;
                    if (py < SMAA_SEARCHTEX_PACKED_HEIGHT && x < SMAA_SEARCHTEX_WIDTH) {
                        out[py * SMAA_SEARCHTEX_WIDTH + x] = val;
                    }
                }

                // Right/down half of texture (y offset by PACKED_HEIGHT + 1).
                int y2 = SMAA_SEARCHTEX_PACKED_HEIGHT + 1 + e1 * 8 + e2 * 4;
                unsigned char val2 = smaa_search_detail::calcSearchValue(e2, e1, 1.0f - bias);
                for (int dy = 0; dy < 4; dy++) {
                    int py = y2 + dy;
                    if (py < SMAA_SEARCHTEX_HEIGHT && x < SMAA_SEARCHTEX_WIDTH) {
                        out[py * SMAA_SEARCHTEX_WIDTH + x] = val2;
                    }
                }
            }
        }
    }

    // Fill the padding columns (64, 65) by repeating column 63.
    for (int y = 0; y < SMAA_SEARCHTEX_HEIGHT; y++) {
        for (int px = SMAA_SEARCHTEX_PACKED_WIDTH; px < SMAA_SEARCHTEX_WIDTH; px++) {
            out[y * SMAA_SEARCHTEX_WIDTH + px] = out[y * SMAA_SEARCHTEX_WIDTH + (SMAA_SEARCHTEX_PACKED_WIDTH - 1)];
        }
    }
}
