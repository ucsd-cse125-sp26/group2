#version 450

layout(location = 0) out vec2 fragUV;

vec2 screenVertices[4] = { vec2(-1,-1),
                            vec2(1,-1),
                            vec2(-1,1),
                            vec2(1,1)
};

vec2 screenVertexUVs[4] = { vec2(0,0),
                            vec2(1,0),
                            vec2(0,1),
                            vec2(1,1)
};

int screenIndices[6] = { 0,1,2,
                         1,3,2
};


void main() {
    gl_Position = vec4(screenVertices[screenIndices[gl_VertexIndex]],0.0f,1.0f);
    fragUV = screenVertexUVs[screenIndices[gl_VertexIndex]];
}