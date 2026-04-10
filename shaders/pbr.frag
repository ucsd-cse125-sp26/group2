// pbr.frag — Cook-Torrance GGX microfacet BRDF with metallic-roughness workflow.
// Output is linear HDR (no tone mapping or gamma here).
#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

layout(location = 0) out vec4 outColor;

// ── Texture samplers ────────────────────────────────────────────────────────
layout(set = 2, binding = 0) uniform sampler2D texAlbedo;
layout(set = 2, binding = 1) uniform sampler2D texMetallicRoughness;
layout(set = 2, binding = 2) uniform sampler2D texEmissive;

// ── Material parameters (pushed per-mesh) ───────────────────────────────────
layout(set = 3, binding = 0) uniform Material
{
    vec4  baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float aoStrength;
    float normalScale;
    vec4  emissiveFactor;   // rgb in xyz, w unused
} mat;

// ── Light data (pushed once per frame) ──────────────────────────────────────
struct Light
{
    vec4 position;   // xyz = dir (w=0 directional) or pos (w=1 point)
    vec4 color;      // rgb = colour, a = intensity
    vec4 params;     // x = range, y = innerCone, z = outerCone, w = castsShadow
};

layout(set = 3, binding = 1) uniform LightData
{
    vec4  cameraPos;     // xyz = world-space eye
    vec4  ambientColor;  // rgb = ambient radiance
    int   numLights;
    float _pad1, _pad2, _pad3;
    Light lights[8];
} lighting;

// ── Constants ───────────────────────────────────────────────────────────────
const float PI = 3.14159265359;

// ── GGX Normal Distribution Function (Trowbridge-Reitz) ────────────────────
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// ── Smith-GGX Geometry Function (Schlick-GGX approximation) ────────────────
float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;   // direct lighting remapping
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// ── Fresnel-Schlick ─────────────────────────────────────────────────────────
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ═══════════════════════════════════════════════════════════════════════════
void main()
{
    // ── Sample textures ─────────────────────────────────────────────────────
    vec4 albedoSample = texture(texAlbedo, fragTexCoord);
    vec3 albedo = albedoSample.rgb * mat.baseColorFactor.rgb;

    // Metallic in B channel, roughness in G channel (glTF convention).
    vec2 mrSample = texture(texMetallicRoughness, fragTexCoord).bg;
    float metallic  = mrSample.x * mat.metallicFactor;
    float roughness = clamp(mrSample.y * mat.roughnessFactor, 0.04, 1.0);

    // ── Surface vectors ─────────────────────────────────────────────────────
    // Normals are pre-transformed in the vertex shader by the correct normal
    // matrix (inverse-transpose).  Mirrored geometry has its winding corrected
    // at load time (ModelLoader::processNode detects negative-determinant
    // transforms and flips index order), so gl_FrontFacing is reliable and
    // no runtime flip is needed here.
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(lighting.cameraPos.xyz - fragWorldPos);

    // ── Fresnel reflectance at normal incidence ─────────────────────────────
    // Dielectrics ~0.04, metals use albedo as F0.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ── Per-light accumulation ──────────────────────────────────────────────
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < lighting.numLights && i < 8; ++i) {
        Light light = lighting.lights[i];
        vec3 L;
        float attenuation = light.color.a;

        if (light.position.w < 0.5) {
            // Directional light — position.xyz is the direction TO the light.
            L = normalize(light.position.xyz);
        } else {
            // Point light.
            vec3 toLight = light.position.xyz - fragWorldPos;
            float dist = length(toLight);
            L = toLight / dist;
            float range = light.params.x;
            attenuation *= max(1.0 - (dist * dist) / (range * range), 0.0);
        }

        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0) {
            // Cook-Torrance specular BRDF.
            float D = distributionGGX(N, H, roughness);
            float G = geometrySmith(N, V, L, roughness);
            vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 numerator   = D * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
            vec3 specular    = numerator / denominator;

            // Energy-conserving diffuse: only non-metallic surfaces diffuse.
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            vec3 diffuse = kD * albedo / PI;

            vec3 radiance = light.color.rgb * attenuation;
            Lo += (diffuse + specular) * radiance * NdotL;
        }
    }

    // ── Ambient (will be replaced by IBL in Phase 6) ────────────────────────
    vec3 ambient = lighting.ambientColor.rgb * albedo;

    // ── Emissive ────────────────────────────────────────────────────────────
    // Only use emissive when the material's emissiveFactor is non-zero.
    // Some game-ripped models store full color detail in "emissive" textures
    // that aren't actually meant to glow — using them blindly washes out the image.
    vec3 emissive = mat.emissiveFactor.rgb;
    if (emissive.r + emissive.g + emissive.b > 0.01)
        emissive *= texture(texEmissive, fragTexCoord).rgb;

    // ── Final colour (linear HDR — no clamp) ────────────────────────────────
    vec3 color = ambient + Lo + emissive;
    outColor = vec4(color, albedoSample.a * mat.baseColorFactor.a);
}
