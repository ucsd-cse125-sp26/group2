/// @file shadow.frag
/// @brief Minimal no-op fragment shader for depth-only shadow passes.
/// SDL3 GPU requires a fragment shader even when only writing depth.
#version 450

void main()
{
    // Depth is written automatically; no colour output needed.
}
