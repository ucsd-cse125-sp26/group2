//#version 450
//layout(location = 0) out vec4 outColor;
//layout(set = 2, binding = 0) uniform samplerCubeArray shadowMap;
//layout(set = 3, binding = 0) uniform FaceParams {
//    int faceIndex;
//    float near;
//    float far;
//} params;
//
//const vec3 faceDirs[6] = vec3[](
//    vec3( 1, 0, 0),
//    vec3(-1, 0, 0),
//    vec3( 0, 1, 0),
//    vec3( 0,-1, 0),
//    vec3( 0, 0, 1),
//    vec3( 0, 0,-1)
//);
//
//void main()
//{
//    // reconstruct a direction for this face from UV
//    vec2 uv = gl_FragCoord.xy / vec2(512.0); // match shadowSize
//    uv = uv * 2.0 - 1.0;
//
//    vec3 dir;
//    int f = params.faceIndex;
//    if      (f == 0) dir = vec3( 1.0,  uv.y, -uv.x); // +X
//    else if (f == 1) dir = vec3(-1.0,  uv.y,  uv.x); // -X
//    else if (f == 2) dir = vec3( uv.x,  1.0, -uv.y); // +Y
//    else if (f == 3) dir = vec3( uv.x, -1.0,  uv.y); // -Y
//    else if (f == 4) dir = vec3( uv.x,  uv.y,  1.0); // +Z
//    else             dir = vec3(-uv.x,  uv.y, -1.0); // -Z
//
//    float d = texture(shadowMap, vec4(dir, float(0))).r;
//    // linearize
//    float linear = (2.0 * params.near) / (params.far + params.near - d * (params.far - params.near));
//    outColor = vec4(vec3(linear), 1.0);
//}
//#version 450
//layout(location = 0) out vec4 outColor;
//void main()
//{
//    outColor = vec4(1.0, 0.0, 0.0, 1.0); // solid red
//}
//#version 450
//layout(location = 0) out vec4 outColor;
//
//layout(set = 3, binding = 0) uniform FaceParams {
//    int faceIndex;
//    float near;
//    float far;
//} params;
//
//void main()
//{
//    float f = float(params.faceIndex) / 6.0;
//    outColor = vec4(f, f, f, 1.0);
//}
//#version 450
//layout(location = 0) out vec4 outColor;
//
//layout(set = 2, binding = 0) uniform samplerCubeArray shadowMap;
//
//layout(set = 3, binding = 0) uniform FaceParams {
//    int faceIndex;
//    float near;
//    float far;
//} params;
//
//const vec3 faceDirs[6] = vec3[](
//    vec3( 1, 0, 0),
//    vec3(-1, 0, 0),
//    vec3( 0, 1, 0),
//    vec3( 0,-1, 0),
//    vec3( 0, 0, 1),
//    vec3( 0, 0,-1)
//);
//
//void main()
//{
//    vec2 uv = gl_FragCoord.xy / vec2(512.0);
//    uv = uv * 2.0 - 1.0;
//
//    int f = params.faceIndex;
//    vec3 dir;
//    if      (f == 0) dir = vec3( 1.0,  uv.y, -uv.x);
//    else if (f == 1) dir = vec3(-1.0,  uv.y,  uv.x);
//    else if (f == 2) dir = vec3( uv.x,  1.0, -uv.y);
//    else if (f == 3) dir = vec3( uv.x, -1.0,  uv.y);
//    else if (f == 4) dir = vec3( uv.x,  uv.y,  1.0);
//    else             dir = vec3(-uv.x,  uv.y, -1.0);
//
//    float d = texture(shadowMap, vec4(dir, 0.0)).r;
//    outColor = vec4(d, d, d, 1.0);
//}








#version 450
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform samplerCubeArray shadowMap;

layout(set = 3, binding = 0) uniform FaceParams {
    int faceIndex;
    float near;
    float far;
} params;

const vec3 faceDirs[6] = vec3[](
    vec3( 1, 0, 0),
    vec3(-1, 0, 0),
    vec3( 0, 1, 0),
    vec3( 0,-1, 0),
    vec3( 0, 0, 1),
    vec3( 0, 0,-1)
);

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(512.0);
    uv = uv * 2.0 - 1.0;

    int f = params.faceIndex;
    vec3 dir;
    if      (f == 0) dir = vec3( 1.0,  uv.y, -uv.x);
    else if (f == 1) dir = vec3(-1.0,  uv.y,  uv.x);
    else if (f == 2) dir = vec3( uv.x,  1.0, -uv.y);
    else if (f == 3) dir = vec3( uv.x, -1.0,  uv.y);
    else if (f == 4) dir = vec3( uv.x,  uv.y,  1.0);
    else             dir = vec3(-uv.x,  uv.y, -1.0);

    float d = texture(shadowMap, vec4(dir, 0.0)).r;
    outColor = vec4(d, d, d, 1.0);
}