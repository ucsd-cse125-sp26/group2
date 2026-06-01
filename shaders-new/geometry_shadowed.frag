#version 450
//#define SHADOW_BIAS 50.0
#define SHADOW_BIAS 5.0
//#define MAX_SPOT_LIGHTS 0
#define MAX_POINT_LIGHTS 6
#define MAX_MOVING_POINT_LIGHTS 64

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
layout(location = 3) in vec4 frag_tangent;

layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D normalTex;
layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessTex;

layout(set = 2, binding = 3) uniform samplerCubeArrayShadow staticPointLightShadowMaps;
layout(set = 2, binding = 4) uniform samplerCubeArrayShadow dynamicPointLightShadowMaps;


layout(set = 3, binding = 0) uniform Material {
    vec4 diffuse;
} material;

layout(set = 3, binding = 1) uniform MaterialFlags {
    uint useTexture;
    uint useNormalTexture;
    uint useMetallicRoughnessTexture;
    uint _pad0;
} materialFlags;

layout(set = 3, binding = 2) uniform lightBlock{
    uint numPointLights;
    uint numMovingPointLights;
    uint numSpotLights;
    float pointLightFarPlane;
    float pointLightNearPlane;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    PointLight pointLights[MAX_POINT_LIGHTS];
    PointLight movingPointLights[MAX_MOVING_POINT_LIGHTS];
} lightInfo;

layout(location = 0) out vec4 color;

// Just a single directional light for now...
//const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
//const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec3 ambient_color = 0.5f * vec3(0.08f, 0.08f,0.12f); // dark-blue
//const vec3 ambient_color = normalize(vec3(0.08f, 0.08f,0.12f)); // dark-blue
//const vec3 ambient_color = vec3(0.0f, 0.0f,0.0f); // dark-black

void main()
{
    vec3 normal = gl_FrontFacing ? frag_normal : -frag_normal;
    if (materialFlags.useNormalTexture != 0) {
        vec3 tangent = normalize(frag_tangent.xyz - normal * dot(normal, frag_tangent.xyz));
        vec3 bitangent = normalize(cross(normal, tangent) * frag_tangent.w);
        mat3 tbn = mat3(tangent, bitangent, normal);
        vec3 tangentNormal = texture(normalTex, frag_vt).xyz * 2.0 - 1.0;
        normal = normalize(tbn * tangentNormal);
    }

    vec4 albedo = materialFlags.useTexture != 0 ? texture(tex, frag_vt) : material.diffuse;
//    albedo.rgb = pow(albedo.rgb,vec3(2.2f));
    vec2 mr = materialFlags.useMetallicRoughnessTexture != 0
        ? texture(metallicRoughnessTex, frag_vt).gb
        : vec2(1.0, 0.0);
    float roughness = mr.x;
    float metallic = mr.y;

    float depthA = lightInfo.pointLightFarPlane / (lightInfo.pointLightFarPlane - lightInfo.pointLightNearPlane );
    float depthB = depthA * lightInfo.pointLightNearPlane;


//    float cosT = max(0.0f, dot(-light_direction, normal));
//    vec4 irradiance = light_color * cosT + ambient_color;
    vec3 irradiance = ambient_color;

    for (int i = 0; i < lightInfo.numPointLights; i++ ){
        PointLight pLight_i = lightInfo.pointLights[i];

        vec3 lightToWorldPos = frag_worldPos - pLight_i.pos;
        float r = length(lightToWorldPos);

        vec3 absDir = abs(lightToWorldPos);
        float dominantAxis = max(absDir.x, max(absDir.y, absDir.z));
        float depth = depthA - depthB / dominantAxis;


        float staticShadow_i = texture(staticPointLightShadowMaps, vec4(lightToWorldPos, float(i)), depth);
        float dynamicShadow_i = texture(dynamicPointLightShadowMaps, vec4(lightToWorldPos, float(i)), depth);

        float shadow_i = dynamicShadow_i * staticShadow_i;

        float attenutaion = 1.0f / (r * r);
        //float attenutaion = 1.0f / (r);

        float cosT_i = max(0.0f, dot(-lightToWorldPos/r, normal));
        irradiance += shadow_i * pLight_i.color * (pLight_i.intensity) * attenutaion * cosT_i;
        //irradiance += shadow_i * pLight_i.color * (.001f * pLight_i.intensity) * attenutaion * cosT_i;

    }

//    albedo.rgb *= (normal * 0.5f) + 0.5f;
    vec3 diffuse = albedo.rgb * (1.0 - metallic) * irradiance;
    vec3 metal = albedo.rgb * metallic * irradiance * (1.0 - 0.5 * roughness);
    color = vec4(diffuse + metal, albedo.a);
    color.rgb = pow(color.rgb,vec3(1.0f/2.2f));
}
