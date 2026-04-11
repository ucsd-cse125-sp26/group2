/// @file geometry.frag
/// @brief Simple lit fragment shader with single directional light.
#version 450

layout(location = 0) in vec3 diffuse;
layout(location = 1) flat in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

const vec3 directionalLight0Direction = normalize(-vec3(1.0f,1.0f,1.0f));
const vec3 directionalLight0Color = vec3(1.0f,1.0f,1.0f);

const vec3 ambientColor = 0.0625f * directionalLight0Color;

void main()
{
    float cosThetaTerm = max(0.0f,dot(-directionalLight0Direction,fragNormal));
    vec3 irradiance = directionalLight0Color * cosThetaTerm + ambientColor;
    outColor = vec4(diffuse * irradiance,1.0f);
}
