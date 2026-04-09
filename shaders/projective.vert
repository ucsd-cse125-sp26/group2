#version 450

layout(location = 0) out vec3 fragColor;

layout(set = 1, binding = 0) uniform Matrices
{
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

const vec3 positions[36] = vec3[](
    vec3(-0.5,-0.5, 0.5), vec3( 0.5,-0.5, 0.5), vec3( 0.5, 0.5, 0.5),
    vec3(-0.5,-0.5, 0.5), vec3( 0.5, 0.5, 0.5), vec3(-0.5, 0.5, 0.5),

    vec3( 0.5,-0.5,-0.5), vec3(-0.5,-0.5,-0.5), vec3(-0.5, 0.5,-0.5),
    vec3( 0.5,-0.5,-0.5), vec3(-0.5, 0.5,-0.5), vec3( 0.5, 0.5,-0.5),

    vec3(-0.5,-0.5,-0.5), vec3(-0.5,-0.5, 0.5), vec3(-0.5, 0.5, 0.5),
    vec3(-0.5,-0.5,-0.5), vec3(-0.5, 0.5, 0.5), vec3(-0.5, 0.5,-0.5),

    vec3( 0.5,-0.5, 0.5), vec3( 0.5,-0.5,-0.5), vec3( 0.5, 0.5,-0.5),
    vec3( 0.5,-0.5, 0.5), vec3( 0.5, 0.5,-0.5), vec3( 0.5, 0.5, 0.5),

    vec3(-0.5, 0.5, 0.5), vec3( 0.5, 0.5, 0.5), vec3( 0.5, 0.5,-0.5),
    vec3(-0.5, 0.5, 0.5), vec3( 0.5, 0.5,-0.5), vec3(-0.5, 0.5,-0.5),

    vec3(-0.5,-0.5,-0.5), vec3( 0.5,-0.5,-0.5), vec3( 0.5,-0.5, 0.5),
    vec3(-0.5,-0.5,-0.5), vec3( 0.5,-0.5, 0.5), vec3(-0.5,-0.5, 0.5)
);

const vec3 colors[6] = vec3[](
    vec3(1,0,0), vec3(0,1,0), vec3(0,0,1),
    vec3(1,1,0), vec3(1,0,1), vec3(0,1,1)
);

void main()
{
    vec4 p = vec4(positions[gl_VertexIndex], 1.0);
    gl_Position = ubo.projection * ubo.view * ubo.model * p;
    fragColor = colors[gl_VertexIndex / 6];
}