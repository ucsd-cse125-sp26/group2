#version 450
//#define SHADOW_BIAS 50.0
#define SHADOW_BIAS 5.0
//#define MAX_SPOT_LIGHTS 0
#define MAX_POINT_LIGHTS 8
// Injected at build time via -DMAX_MOVING_POINT_LIGHTS from CMakeLists.txt
// (56 on macOS/Metal, 64 elsewhere). Must match Boilerplate.hpp. The default
// below is only a fallback for editor/standalone compiles.
#ifndef MAX_MOVING_POINT_LIGHTS
#define MAX_MOVING_POINT_LIGHTS 64
#endif

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
layout(location = 4) in vec2 frag_lt;

layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D normalTex;
layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessTex;

layout(set = 2, binding = 3) uniform samplerCubeArrayShadow staticPointLightShadowMaps;
layout(set = 2, binding = 4) uniform samplerCubeArrayShadow dynamicPointLightShadowMaps;
layout(set = 2, binding = 5) uniform samplerCubeArrayShadow movingPointLightShadowMaps;


layout(set = 3, binding = 0) uniform Material {
    vec4 diffuse;
} material;

layout(set = 3, binding = 1) uniform MaterialFlags {
    uint useTexture;
    uint useNormalTexture;
    uint useMetallicRoughnessTexture;
    uint useTint;
} materialFlags;

layout(set = 3, binding = 2) uniform lightBlock{
    uint numPointLights;
    uint numMovingPointLights;
    uint numSpotLights;
    float pointLightFarPlane;
    float pointLightNearPlane;
    float cameraPosX;
    float cameraPosY;
    float cameraPosZ;
    PointLight pointLights[MAX_POINT_LIGHTS];
    PointLight movingPointLights[MAX_MOVING_POINT_LIGHTS];
} lightInfo;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normalColor;

// Just a single directional light for now...
//const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
//const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec3 ambient_color = 0.5f * vec3(0.08f, 0.08f,0.12f); // dark-blue
//const vec3 ambient_color = normalize(vec3(0.08f, 0.08f,0.12f)); // dark-blue
//const vec3 ambient_color = vec3(0.0f, 0.0f,0.0f); // dark-black

float roughnessToShininess(float roughness)
{
    return mix(256.0, 4.0, clamp(roughness, 0.0, 1.0));
}

vec3 specularTint(vec3 albedo, float metallic)
{
    return mix(vec3(0.04), albedo, clamp(metallic, 0.0, 1.0));
}

void main()
{
    vec3 normal = normalize(gl_FrontFacing ? frag_normal : -frag_normal);
    if (materialFlags.useNormalTexture != 0) {
        vec3 tangent = normalize(frag_tangent.xyz - normal * dot(normal, frag_tangent.xyz));
        vec3 bitangent = normalize(cross(normal, tangent) * frag_tangent.w);
        mat3 tbn = mat3(tangent, bitangent, normal);
        vec3 tangentNormal = texture(normalTex, frag_vt).xyz * 2.0 - 1.0;
        normal = normalize(tbn * tangentNormal);
    }

    vec4 albedo = materialFlags.useTexture != 0 ? texture(tex, frag_vt) : material.diffuse;
//    albedo.rgb = pow(albedo.rgb,vec3(2.2f));
    if (materialFlags.useTint != 0) {
        albedo.rgb = mix(albedo.rgb, pow(material.diffuse.rgb, vec3(2.2f)), material.diffuse.a);
    }
    vec2 mr = materialFlags.useMetallicRoughnessTexture != 0
        ? texture(metallicRoughnessTex, frag_vt).gb
        : vec2(0.5, 0.0);
    float roughness = clamp(mr.x, 0.0, 1.0);
    float metallic = clamp(mr.y, 0.0, 1.0);

//    float depthA = lightInfo.pointLightFarPlane / (lightInfo.pointLightFarPlane - lightInfo.pointLightNearPlane );
//    float depthB = depthA * lightInfo.pointLightNearPlane;


    float depthA = lightInfo.pointLightNearPlane / (lightInfo.pointLightNearPlane - lightInfo.pointLightFarPlane );
    float depthB = depthA * lightInfo.pointLightFarPlane;


//    float cosT = max(0.0f, dot(-light_direction, normal));
//    vec4 irradiance = light_color * cosT + ambient_color;
    vec3 diffuseIrradiance = ambient_color;
    vec3 specularIrradiance = vec3(0.0);
    vec3 cameraPos = vec3(lightInfo.cameraPosX, lightInfo.cameraPosY, lightInfo.cameraPosZ);
    vec3 viewDir = normalize(cameraPos - frag_worldPos);
    float shininess = roughnessToShininess(roughness);
    float specularStrength = mix(1.0, 0.12, roughness);
    vec3 specTint = specularTint(albedo.rgb, metallic);

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

        vec3 lightDir = -lightToWorldPos / r;
        float cosT_i = max(0.0f, dot(lightDir, normal));
        vec3 lightRadiance = shadow_i * pLight_i.color * (pLight_i.intensity) * attenutaion;
        diffuseIrradiance += lightRadiance * cosT_i;

        vec3 halfDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(normal, halfDir), 0.0);
        float specular = pow(specAngle, shininess) * specularStrength * cosT_i;
        specularIrradiance += lightRadiance * specular;

    }
    for (int i = 0; i < lightInfo.numMovingPointLights; i++ ){
        PointLight pLight_i = lightInfo.movingPointLights[i];

        vec3 lightToWorldPos = frag_worldPos - pLight_i.pos;
        float r = length(lightToWorldPos);

        vec3 absDir = abs(lightToWorldPos);
        float dominantAxis = max(absDir.x, max(absDir.y, absDir.z));
        float depth = depthA - depthB / dominantAxis;

        float shadow_i = texture(movingPointLightShadowMaps, vec4(lightToWorldPos, float(i)), depth);

        float attenutaion = 1.0f / (r * r);

        vec3 lightDir = -lightToWorldPos / r;
        float cosT_i = max(0.0f, dot(lightDir, normal));
        vec3 lightRadiance = shadow_i * pLight_i.color * (pLight_i.intensity) * attenutaion;
        diffuseIrradiance += lightRadiance * cosT_i;

        vec3 halfDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(normal, halfDir), 0.0);
        float specular = pow(specAngle, shininess) * specularStrength * cosT_i;
        specularIrradiance += lightRadiance * specular;

    }

    vec3 diffuse = albedo.rgb * (1.0 - metallic) * diffuseIrradiance;
    vec3 specular = specTint * specularIrradiance;
    color = vec4(diffuse + specular, albedo.a);
    normalColor = vec4(normal * 0.5 + 0.5, 1.0);
}
