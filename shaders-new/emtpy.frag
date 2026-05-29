//#version 450
//
//layout(location = 0) in vec3 frag_worldPos;
//
//layout(set = 3, binding = 0) uniform ShadowLightInfo
//{
//    vec3 lightPos;
//    float farPlane;
//} shadowInfo;
//
//void main()
//{
//    vec3 lightToFrag = frag_worldPos - shadowInfo.lightPos;
//
//    // Linear radial depth for point-light cubemap shadows
//    gl_FragDepth = length(lightToFrag) / shadowInfo.farPlane;
//}
#version 450
void main() {

}
