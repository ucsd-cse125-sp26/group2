/// @file hud.vert
/// @brief HUD vertex shader — converts pixel coordinates to clip space.
#version 450

layout(location = 0) in vec2  inPosition;   // pixel coords, origin top-left
layout(location = 1) in vec2  inUV;
layout(location = 2) in vec4  inColor;
layout(location = 3) in float inTexMode;
layout(location = 4) in vec3  inShapeData;  // mode 3: halfW, halfH, radius

layout(set = 1, binding = 0) uniform HudUniforms {
    vec2 screenSize;    // viewport width, height in pixels
};

layout(location = 0) out vec2  vUV;
layout(location = 1) out vec4  vColor;
layout(location = 2) out float vTexMode;
layout(location = 3) out vec3  vShapeData;

void main()
{
    // Pixel coords → NDC.  Y is flipped so (0,0) = top-left.
    vec2 ndc;
    ndc.x =  (inPosition.x / screenSize.x) * 2.0 - 1.0;
    ndc.y = -((inPosition.y / screenSize.y) * 2.0 - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);

    vUV        = inUV;
    vColor     = inColor;
    vTexMode   = inTexMode;
    vShapeData = inShapeData;
}
