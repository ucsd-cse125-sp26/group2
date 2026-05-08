#version 450

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D hudTex;

void main() {
    color = texture(hudTex,fragUV);
//    if (color.w < 0.5f){
//        color = vec4(1.0f);
//    }
}