// fullscreen.vert — generates a fullscreen triangle from gl_VertexIndex.
// Draw with 3 vertices, no vertex buffer needed.
#version 450

layout(location = 0) out vec2 fragTexCoord;

void main()
{
    // Triangle covers the entire [-1,1]² NDC quad:
    //   vertex 0 → (-1, -1)   texCoord (0, 0)
    //   vertex 1 → ( 3, -1)   texCoord (2, 0)
    //   vertex 2 → (-1,  3)   texCoord (0, 2)
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position  = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord = pos;
}
