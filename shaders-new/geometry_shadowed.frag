#version 450
//#define SHADOW_BIAS 50.0
#define SHADOW_BIAS 5.0
//#define MAX_SPOT_LIGHTS 0
#define MAX_POINT_LIGHTS 4

struct PointLight {
    vec3 pos;
    float intensity;
    vec3 color;
    float range;
};

struct LightUBO {
    uint numPointLights;
    uint numSpotLights;
    float pointLightFarPlane;
    float pointLightNearPlane;
    PointLight pointLights[MAX_POINT_LIGHTS];
};

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;
layout(location = 2) in vec3 frag_worldPos;

layout(set = 2, binding = 0) uniform sampler2D tex;

//layout(set = 2, binding = 1) uniform samplerCubeArray pointLightShadowMaps;
layout(set = 2, binding = 1) uniform samplerCubeArrayShadow pointLightShadowMaps;
//layout(set = 2, binding = 1) uniform sampler2DArrayShadow ;

layout(set = 3, binding = 0) uniform Material {
    vec4 diffuse;
} material;

layout(set = 3, binding = 1) uniform MaterialFlags {
    uint useTexture;
} materialFlags;

layout(set = 3, binding = 2) uniform lightBlock{
    uint numPointLights;
    uint numSpotLights;
    float pointLightFarPlane;
    float pointLightNearPlane;
    PointLight pointLights[MAX_POINT_LIGHTS];
    mat4 pointLightFaceTransforms[MAX_POINT_LIGHTS*6];
} lightInfo;

layout(location = 0) out vec4 color;

// Just a single directional light for now...
//const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
//const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
//const vec3 ambient_color = normalize(vec3(0.08f, 0.08f,0.12f)); // dark-blue
const vec3 ambient_color = vec3(0.0f, 0.0f,0.0f); // dark-black

void main()
{
    vec3 normal = gl_FrontFacing ? frag_normal : -frag_normal;

    vec4 albedo = materialFlags.useTexture != 0 ? texture(tex, frag_vt) : material.diffuse;
    albedo.rgb = pow(albedo.rgb,vec3(2.2f));

    //vec3 frag_worldPosFinal = vec3(frag_worldPos.x,-frag_worldPos.y,frag_worldPos.z);
    vec3 frag_worldPosFinal = frag_worldPos;
    //vec3 frag_worldPosFinal = frag_worldPos;

    float depthA = lightInfo.pointLightFarPlane / (lightInfo.pointLightFarPlane - lightInfo.pointLightNearPlane );
    float depthB = depthA * lightInfo.pointLightNearPlane;


//    float cosT = max(0.0f, dot(-light_direction, normal));
//    vec4 irradiance = light_color * cosT + ambient_color;
    vec3 irradiance = ambient_color;

    for (int i = 0; i < lightInfo.numPointLights; i++ ){
        PointLight pLight_i = lightInfo.pointLights[i];
        //vec3 lightPosFinal = vec3(pLight_i.pos.x,-pLight_i.pos.y,pLight_i.pos.z);
        vec3 lightPosFinal = pLight_i.pos;

        //vec3 normalFinal = vec3(normal.x,-normal.y,normal.z);
        vec3 normalFinal = normal;

        vec3 lightToWorldPos = frag_worldPos - pLight_i.pos;
        //vec3 biasedLightToWorldPos = (frag_worldPosFinal + normalFinal * SHADOW_BIAS) - lightPosFinal;
        vec3 biasedLightToWorldPos = lightToWorldPos;

        float r = length(lightToWorldPos);

        float depth = depthA - depthB / r ;


        float biasFactor = 0.005; // tune this, much smaller than world-space bias
        float cosT_i = max(0.0f, dot(-lightToWorldPos/r, normal));
        float biasedDepth = depth - biasFactor * (1.0 - cosT_i); // more bias on grazing angles

        float shadow_i = texture(pointLightShadowMaps,vec4(biasedLightToWorldPos,float(i)),biasedDepth);
        //float shadow_i = texture(pointLightShadowMaps,vec4(biasedLightToWorldPos,float(i)),depth);
        //float shadow_i = 1.0f;

        float attenutaion = 1.0f / (r * r);

        irradiance += shadow_i * pLight_i.color * pLight_i.intensity * attenutaion * cosT_i;

//        vec4 sampledValue = texture(pointLightShadowMaps, vec4(lightToWorldPos, float(i)));
//        float storedDepth = sampledValue.r;
//        color = vec4(storedDepth, storedDepth, storedDepth, 1.0);
//        return;
    }

    //albedo.rgb *= (normal * 0.5f) + 0.5f;
    color = albedo * vec4(irradiance,1.0f);
    color.rgb = pow(color.rgb,vec3(1.0f/2.2f));
}
