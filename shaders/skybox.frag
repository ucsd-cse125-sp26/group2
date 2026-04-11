// skybox.frag — procedural gradient sky with sun disc.
// Outputs linear HDR colour (tone mapped in a later pass).
// Will be replaced with a cubemap sampler when environment maps are loaded.
#version 450

layout(location = 0) in vec3 fragDir;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 dir = normalize(fragDir);

    // Vertical blend factor: -1 (nadir) … 0 (horizon) … +1 (zenith).
    float y = dir.y;

    // ── Sky gradient ────────────────────────────────────────────────────────
    vec3 zenith  = vec3(0.08, 0.16, 0.45);   // deep blue
    vec3 horizon = vec3(0.6,  0.45, 0.35);    // warm haze
    vec3 nadir   = vec3(0.03, 0.03, 0.05);    // dark ground

    vec3 sky;
    if (y > 0.0) {
        // Above horizon: blend horizon → zenith.
        float t = pow(y, 0.4);   // ease toward horizon (more horizon colour)
        sky = mix(horizon, zenith, t);
    } else {
        // Below horizon: blend horizon → nadir.
        float t = pow(-y, 0.6);
        sky = mix(horizon, nadir, t);
    }

    // ── Sun disc ────────────────────────────────────────────────────────────
    // Direction toward the sun (matches the primary PBR light direction).
    vec3  sunDir   = normalize(vec3(0.5, 0.3, 0.8));
    float sunAngle = dot(dir, sunDir);

    // Soft disc with a bright core.
    float sunDisc = smoothstep(0.9975, 0.999, sunAngle);  // sharp disc edge
    float sunGlow = pow(max(sunAngle, 0.0), 256.0);       // tight halo

    // HDR sun: subtle bloom, not scene-dominating.
    vec3 sunColor = vec3(1.0, 0.95, 0.85) * 4.0;
    sky += sunColor * sunDisc + vec3(1.0, 0.8, 0.5) * sunGlow * 0.25;

    // ── Horizon glow (subtle) ───────────────────────────────────────────────
    float horizonGlow = exp(-abs(y) * 4.0);
    sky += vec3(0.3, 0.2, 0.1) * horizonGlow * 0.15;

    outColor = vec4(sky, 1.0);
}
