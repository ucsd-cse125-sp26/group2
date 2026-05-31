#version 450

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;
layout(location = 2) in vec3 frag_worldPos;
layout(location = 3) in vec4 frag_tangent;

layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D normalTex;
layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessTex;

layout(set = 3, binding = 0) uniform Material {
    vec4 diffuse;
} material;

layout(set = 3, binding = 1) uniform MaterialFlags {
    uint useTexture;
    uint useNormalTexture;
    uint useMetallicRoughnessTexture;
    uint _pad0;
} materialFlags;

// Just a single directional light for now...
const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec4 ambient_color = vec4(normalize(vec3(0.08f, 0.08f,0.12f)),1.0f); // dark-blue

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
    vec2 mr = materialFlags.useMetallicRoughnessTexture != 0
        ? texture(metallicRoughnessTex, frag_vt).gb
        : vec2(1.0, 0.0);
    float roughness = mr.x;
    float metallic = mr.y;

    float cosT = max(0.0f, dot(-light_direction, normal));
    vec4 irradiance = light_color * cosT + ambient_color;

    vec3 diffuse = albedo.rgb * (1.0 - metallic) * irradiance.rgb;
    vec3 metal = albedo.rgb * metallic * light_color.rgb * cosT * (1.0 - 0.5 * roughness);
    color = vec4(diffuse + metal, albedo.a);
}
