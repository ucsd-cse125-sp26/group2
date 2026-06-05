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
    uint useTint;
} materialFlags;

// Just a single directional light for now...
const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec4 ambient_color = vec4(normalize(vec3(0.08f, 0.08f,0.12f)),1.0f); // dark-blue

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
    if (materialFlags.useTint != 0) {
        albedo.rgb = mix(albedo.rgb, material.diffuse.rgb, material.diffuse.a);
    }
    vec2 mr = materialFlags.useMetallicRoughnessTexture != 0
        ? texture(metallicRoughnessTex, frag_vt).gb
        : vec2(0.5, 0.0);
    float roughness = clamp(mr.x, 0.0, 1.0);
    float metallic = clamp(mr.y, 0.0, 1.0);

    float cosT = max(0.0f, dot(-light_direction, normal));
    vec4 irradiance = light_color * cosT + ambient_color;

    vec3 diffuse = albedo.rgb * (1.0 - metallic) * irradiance.rgb;
    vec3 specularTint = mix(vec3(0.04), albedo.rgb, metallic);
    vec3 viewDir = normalize(-frag_worldPos);
    vec3 halfDir = normalize(-light_direction + viewDir);
    float shininess = mix(256.0, 4.0, roughness);
    float specularStrength = mix(1.0, 0.12, roughness);
    float specular = pow(max(dot(normal, halfDir), 0.0), shininess) * specularStrength * cosT;
    color = vec4(diffuse + specularTint * light_color.rgb * specular, albedo.a);
}
