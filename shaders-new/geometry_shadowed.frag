#version 450
#define SHADOW_BIAS 0.0001
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
    float padding_0;
    PointLight pointLights[MAX_POINT_LIGHTS];
};

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;
layout(location = 2) in vec3 frag_worldPos;


layout(set = 2, binding = 0) uniform sampler2D tex;

layout(set = 2, binding = 1) uniform samplerCubeArrayShadow pointLightShadowMaps;
//layout(set = 2, binding = 1) uniform sampler2DArrayShadow ;

layout(set = 3, binding = 0) uniform Material {
    vec4 diffuse;
} material;

layout(set = 3, binding = 1) uniform MaterialFlags {
    uint useTexture;
} materialFlags;

layout(set = 3, binding = 2) uniform LightUBO lightInfo;

layout(location = 0) out vec4 color;

// Just a single directional light for now...
//const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
//const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec3 ambient_color = normalize(vec3(0.08f, 0.08f,0.12f)); // dark-blue

void main()
{
    vec3 normal = gl_FrontFacing ? frag_normal : -frag_normal;

    vec4 albedo = materialFlags.useTexture != 0 ? texture(tex, frag_vt) : material.diffuse;


//    float cosT = max(0.0f, dot(-light_direction, normal));
//    vec4 irradiance = light_color * cosT + ambient_color;
    vec3 irradiance = ambient_color;

    for (int i = 0; i < lightInfo.numPointLights; i++ ){
        PointLight pLight_i = lightInfo.pointLights[i];

        vec3 lightToWorldPos = frag_worldPos - pLight_i.pos;
        vec3 biasedLightToWorldPos = (frag_worldPos + normal * SHADOW_BIAS) - pLight_i.pos;

        float r = length(lightToWorldPos);
        float depth = r / lightInfo.pointLightFarPlane;

        float shadow_i = texture(pointLightShadowMaps,vec4(biasedLightToWorldPos,float(i)),depth);

        float cosT_i = max(0.0f, dot(-lightToWorldPos/r, normal));
        float attenutaion = 1.0f / (r * r);

        irradiance += shadow_i * pLight_i.color * pLight_i.intensity * attenutaion * cosT_i;
    }

    albedo.rgb *= (normal * 0.5f) + 0.5f;
    color = albedo * vec4(irradiance,1.0f);
}
