/// @file smaa_fullscreen.vert
/// @brief Fullscreen triangle for SMAA render passes.
///
/// Identical to fullscreen.vert except fragTexCoord.y is flipped so that
/// the SMAA fragment-shader outputs land in the same Y convention as the
/// compute-shader post-processing textures (bloom, SSAO, SSR, volumetrics).
///
/// Without this flip the SMAA render-pass outputs would be vertically
/// inverted relative to hdrTarget, because hdrTarget is rendered with
/// both the SDL3 GPU viewport flip AND a projection Y-flip (proj[1][1]*=-1),
/// while the SMAA passes only have the viewport flip.
#version 450

layout(location = 0) out vec2 fragTexCoord;

void main()
{
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position  = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord = vec2(pos.x, 1.0 - pos.y);
}
