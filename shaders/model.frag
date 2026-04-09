// model.frag — fragment shader for Assimp-loaded mesh geometry.
//
// Applies a two-light diffuse + ambient shading model in world space.
// No textures yet; base colour is a warm grey.
#version 450

layout(location = 0) in  vec3 fragNormal;
layout(location = 1) in  vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 n = normalize(fragNormal);

    // Primary light — top-right-front
    vec3  dir0  = normalize(vec3( 0.5,  1.0,  0.5));
    float diff0 = max(dot(n, dir0), 0.0);

    // Secondary fill light — left-back, weaker
    vec3  dir1  = normalize(vec3(-0.5,  0.3, -0.8));
    float diff1 = max(dot(n, dir1), 0.0) * 0.35;

    float ambient  = 0.25;
    float lighting = ambient + diff0 * 0.65 + diff1;

    // Warm grey base colour
    vec3 baseColor = vec3(0.76, 0.73, 0.70);
    outColor = vec4(baseColor * lighting, 1.0);
}
