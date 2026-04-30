/// @file hud_blit.frag
/// @brief Fullscreen blit of HUD overlay texture with alpha.
#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D hudTexture;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(hudTexture, fragTexCoord);
}
