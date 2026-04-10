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
layout(set = 2, binding = 3) uniform sampler2D texNormal;
// IBL textures (Phase 6).
layout(set = 2, binding = 4) uniform samplerCube irradianceMap;
layout(set = 2, binding = 5) uniform samplerCube prefilterMap;
layout(set = 2, binding = 6) uniform sampler2D   brdfLUT;
// Shadow map (Phase 2).
layout(set = 2, binding = 7) uniform sampler2DShadow shadowMap;

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

// ── Shadow data (pushed once per frame) ─────────────────────────────────────
layout(set = 3, binding = 2) uniform ShadowData
{
    mat4  lightVP;
    float shadowBias;
    float shadowNormalBias;
    float shadowMapSize;
    float _shadowPad;
} shadow;

// ── Constants ───────────────────────────────────────────────────────────────
const float PI = 3.14159265359;

// ── Shadow sampling (3×3 PCF) ───────────────────────────────────────────────
float calcShadow(vec3 worldPos, vec3 N)
{
    // Transform world position to light clip space.
    vec4 lightClip = shadow.lightVP * vec4(worldPos, 1.0);
    vec3 lightNDC = lightClip.xyz / lightClip.w;

    // NDC [-1,1] → UV [0,1].  Vulkan depth is already [0,1].
    vec2 shadowUV = lightNDC.xy * 0.5 + 0.5;
    float currentDepth = lightNDC.z;

    // Outside shadow map → fully lit.
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;

    // 3×3 PCF with comparison sampler.
    float texelSize = 1.0 / shadow.shadowMapSize;
    float total = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            // sampler2DShadow: texture() returns 0.0 (in shadow) or 1.0 (lit).
            total += texture(shadowMap, vec3(shadowUV + offset, currentDepth - shadow.shadowBias));
        }
    }
    return total / 9.0;
}

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

// Roughness-aware Fresnel for IBL (reduces rim glow on rough surfaces).
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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
    // Sample normal map if present (flat-normal fallback texture = (0.5, 0.5, 1.0)
    // which produces N = (0, 0, 1) in tangent space = no perturbation).
    vec3 tangentN = texture(texNormal, fragTexCoord).rgb * 2.0 - 1.0;
    tangentN.xy *= mat.normalScale;
    tangentN = normalize(tangentN);

    // TBN matrix: transform tangent-space normal to world space.
    mat3 TBN = mat3(normalize(fragTangent), normalize(fragBitangent), normalize(fragNormal));
    vec3 N = normalize(TBN * tangentN);

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
            // Clamp NdotV in the denominator to prevent specular blow-up at
            // grazing angles (exaggerated by normal maps at mesh edges).
            float denominator = 4.0 * max(dot(N, V), 0.1) * NdotL + 0.0001;
            vec3 specular    = numerator / denominator;

            // Energy-conserving diffuse: only non-metallic surfaces diffuse.
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            vec3 diffuse = kD * albedo / PI;

            vec3 radiance = light.color.rgb * attenuation;

            // Apply shadow only to the primary directional light (index 0).
            float shadowFactor = 1.0;
            if (i == 0 && shadow.shadowMapSize > 0.0)
                shadowFactor = calcShadow(fragWorldPos, N);

            Lo += (diffuse + specular) * radiance * NdotL * shadowFactor;
        }
    }

    // ── Image-Based Lighting (IBL) ─────────────────────────────────────────
    // Split-sum approximation: the integral of incoming environment radiance
    // is split into a pre-filtered specular term and a diffuse irradiance term.
    // Clamp NdotV to avoid extreme Fresnel rim glow from normal maps
    // that push normals nearly perpendicular to the view direction.
    float NdotV_ibl = max(dot(N, V), 0.05);
    vec3 F_ibl = fresnelSchlickRoughness(NdotV_ibl, F0, roughness);

    // Metallic surfaces don't diffuse; dielectrics do.
    vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);

    // Diffuse IBL: irradiance cubemap sampled along the normal direction.
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL = kD_ibl * albedo * irradiance;

    // Specular IBL: pre-filtered environment map sampled along reflection.
    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0; // matches the number of prefilter mip levels
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV_ibl, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    vec3 ambient = diffuseIBL + specularIBL;

    // ── Emissive ────────────────────────────────────────────────────────────
    // Only use emissive when the material's emissiveFactor is non-zero.
    // Some game-ripped models store full color detail in "emissive" textures
    // that aren't actually meant to glow — using them blindly washes out the image.
    vec3 emissive = mat.emissiveFactor.rgb;
    if (emissive.r + emissive.g + emissive.b > 0.01)
        emissive *= texture(texEmissive, fragTexCoord).rgb;

    // ── Final colour ────────────────────────────────────────────────────────
    // The opaque pipeline ignores alpha (blending off).
    // The transparent pipeline uses it (alpha blending on, no depth write).
    // Alpha comes from baseColorFactor.a × albedo texture alpha.
    vec3 color = ambient + Lo + emissive;
    float alpha = mat.baseColorFactor.a * albedoSample.a;
    outColor = vec4(color, alpha);
}
