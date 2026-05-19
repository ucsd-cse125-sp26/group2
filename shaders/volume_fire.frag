/// @file volume_fire.frag
/// @brief Front-to-back raymarch shader for animated normalized temperature volumes.
#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) flat in vec3 vBoxMin;
layout(location = 2) flat in vec3 vBoxMax;
layout(location = 3) flat in float vOpacity;
layout(location = 4) flat in vec3 vCamPos;

layout(set = 2, binding = 0) uniform sampler3D fireFrame0;
layout(set = 2, binding = 1) uniform sampler3D fireFrame1;

layout(set = 3, binding = 0) uniform VolumeFireParams {
    vec4 dimsAndBlend;
    vec4 render;
    vec4 tint;
} vf;

layout(location = 0) out vec4 outColor;

vec2 intersectAabb(vec3 origin, vec3 dir, vec3 bmin, vec3 bmax)
{
    vec3 safeDir = vec3(abs(dir.x) < 1e-6 ? 1e-6 : dir.x,
                        abs(dir.y) < 1e-6 ? 1e-6 : dir.y,
                        abs(dir.z) < 1e-6 ? 1e-6 : dir.z);
    vec3 invDir = 1.0 / safeDir;
    vec3 t0 = (bmin - origin) * invDir;
    vec3 t1 = (bmax - origin) * invDir;
    vec3 tsmaller = min(t0, t1);
    vec3 tbigger = max(t0, t1);
    return vec2(max(max(tsmaller.x, tsmaller.y), tsmaller.z),
                min(min(tbigger.x, tbigger.y), tbigger.z));
}

vec3 fireColor(float t)
{
    vec3 ember = vec3(0.82, 0.055, 0.01);
    vec3 orange = vec3(1.0, 0.29, 0.035);
    vec3 gold = vec3(1.0, 0.61, 0.13);
    vec3 hot = vec3(1.0, 0.78, 0.34);
    vec3 c = mix(ember, orange, smoothstep(0.04, 0.38, t));
    c = mix(c, gold, smoothstep(0.34, 0.78, t));
    c = mix(c, hot, smoothstep(0.86, 1.0, t));
    return c;
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    vec3 dir = normalize(vWorldPos - vCamPos);
    vec2 hit = intersectAabb(vCamPos, dir, vBoxMin, vBoxMax);
    float tNear = max(hit.x, 0.0);
    float tFar = hit.y;
    if (tFar <= tNear)
        discard;

    int steps = int(clamp(vf.render.x, 16.0, 192.0));
    float rayLen = tFar - tNear;
    float dt = rayLen / float(steps);
    vec4 accum = vec4(0.0);
    float jitter = hash12(gl_FragCoord.xy + vec2(vf.dimsAndBlend.w * 97.13, vf.dimsAndBlend.w * 31.71));

    for (int i = 0; i < 192; ++i) {
        if (i >= steps || accum.a > 0.985)
            break;

        float t = tNear + (float(i) + jitter) * dt;
        vec3 world = vCamPos + dir * t;
        vec3 local = clamp((world - vBoxMin) / (vBoxMax - vBoxMin), vec3(0.0), vec3(1.0));
        vec3 uvw = vec3(local.x, local.z, local.y);
        float density0 = texture(fireFrame0, uvw).r;
        float density1 = texture(fireFrame1, uvw).r;
        float density = mix(density0, density1, vf.dimsAndBlend.w);
        density = pow(smoothstep(0.018, 0.82, density), 1.12);

        float alpha = clamp(pow(density, 1.25) * vf.render.y * 0.028, 0.0, 0.12);
        vec3 color = mix(vec3(0.16, 0.0, 0.002), vf.tint.rgb, smoothstep(0.05, 0.78, density)) * vf.render.z;
        accum.rgb += (1.0 - accum.a) * color * alpha;
        accum.a += (1.0 - accum.a) * alpha;
    }

    accum *= clamp(vOpacity * vf.render.w, 0.0, 1.0);
    if (accum.a <= 0.002)
        discard;
    outColor = accum;
}
