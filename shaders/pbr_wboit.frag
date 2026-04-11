// pbr_wboit.frag — Weighted Blended OIT accumulation pass.
// Outputs to two render targets: accumulation (RGBA16F) and revealage (R8).
// This is the SAME PBR lighting as pbr.frag but writes to OIT buffers.
// For now, this is a simplified version that just does the OIT weighting.
#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

// Two color outputs for MRT (accumulation + revealage).
layout(location = 0) out vec4 accumulation;
layout(location = 1) out vec4 revealage;

layout(set = 2, binding = 0) uniform sampler2D texAlbedo;

layout(set = 3, binding = 0) uniform Material {
    vec4  baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float aoStrength;
    float normalScale;
    vec4  emissiveFactor;
} mat;

void main()
{
    vec4 albedoSample = texture(texAlbedo, fragTexCoord);
    vec3 color = albedoSample.rgb * mat.baseColorFactor.rgb;
    float alpha = mat.baseColorFactor.a * albedoSample.a;

    // Weight function (McGuire & Bavoil 2013).
    // Biases by depth so nearer transparent surfaces have more influence.
    float weight = alpha * max(0.01, 3000.0 * pow(1.0 - gl_FragCoord.z, 3.0));

    // Accumulation: premultiplied color × weight.
    accumulation = vec4(color * alpha * weight, alpha * weight);

    // Revealage: product of (1 - alpha).
    revealage = vec4(alpha);
}
