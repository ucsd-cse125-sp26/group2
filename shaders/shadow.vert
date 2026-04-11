// shadow.vert — depth-only vertex shader for shadow map rendering.
// No fragment shader needed (depth-only pass).
#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 1, binding = 0) uniform LightMatrices
{
    mat4 lightVP;
    mat4 model;
};

void main()
{
    gl_Position = lightVP * model * vec4(inPosition, 1.0);
}
