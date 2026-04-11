// skybox.frag — procedural gradient sky OR HDR cubemap environment.
// Outputs linear HDR colour (tone mapped in a later pass).
#version 450

layout(location = 0) in vec3 fragDir;
layout(location = 0) out vec4 outColor;

// HDR environment cubemap (bound even in procedural mode — uses fallback).
layout(set = 2, binding = 0) uniform samplerCube envMap;

layout(set = 3, binding = 0) uniform SkyboxParams
{
    int   useCubemap;
    float envExposure;
    float _pad1, _pad2;
    vec4  sunDir;          // xyz = direction TO sun (from C++ getSunDirection)
};

void main()
{
    vec3 dir = normalize(fragDir);

    if (useCubemap > 0) {
        vec3 color = texture(envMap, dir).rgb * envExposure;
        outColor = vec4(color, 1.0);
        return;
    }

    // ── Procedural gradient sky (fallback) ──────────────────────────────────
    float y = dir.y;

    vec3 zenith  = vec3(0.08, 0.16, 0.45);
    vec3 horizon = vec3(0.6,  0.45, 0.35);
    vec3 nadir   = vec3(0.03, 0.03, 0.05);

    vec3 sky;
    if (y > 0.0) {
        float t = pow(y, 0.4);
        sky = mix(horizon, zenith, t);
    } else {
        float t = pow(-y, 0.6);
        sky = mix(horizon, nadir, t);
    }

    // Sun disc — position follows the sun direction from the UBO.
    vec3  sd = normalize(sunDir.xyz);
    float sunAngle = dot(dir, sd);
    float sunDisc = smoothstep(0.9975, 0.999, sunAngle);
    float sunGlow = pow(max(sunAngle, 0.0), 256.0);

    vec3 sunColor = vec3(1.0, 0.95, 0.85) * 4.0;
    sky += sunColor * sunDisc + vec3(1.0, 0.8, 0.5) * sunGlow * 0.25;

    // Horizon glow (subtle).
    float horizonGlow = exp(-abs(y) * 4.0);
    sky += vec3(0.3, 0.2, 0.1) * horizonGlow * 0.15;

    outColor = vec4(sky, 1.0);
}
