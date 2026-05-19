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
    vec3 deep = vec3(1.0, 0.12, 0.015);
    vec3 orange = vec3(1.0, 0.42, 0.055);
    vec3 yellow = vec3(1.0, 0.86, 0.22);
    vec3 white = vec3(1.0, 0.96, 0.78);
    vec3 c = mix(deep, orange, smoothstep(0.05, 0.35, t));
    c = mix(c, yellow, smoothstep(0.28, 0.70, t));
    c = mix(c, white, smoothstep(0.72, 1.0, t));
    return c;
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

    for (int i = 0; i < 192; ++i) {
        if (i >= steps || accum.a > 0.985)
            break;

        float t = tNear + (float(i) + 0.5) * dt;
        vec3 world = vCamPos + dir * t;
        vec3 uvw = clamp((world - vBoxMin) / (vBoxMax - vBoxMin), vec3(0.0), vec3(1.0));
        float temp0 = texture(fireFrame0, uvw).r;
        float temp1 = texture(fireFrame1, uvw).r;
        float temp = mix(temp0, temp1, vf.dimsAndBlend.w);
        temp = smoothstep(0.018, 0.92, temp);

        float alpha = clamp(temp * temp * vf.render.y * 0.035, 0.0, 0.22);
        vec3 color = fireColor(temp) * vf.render.z;
        accum.rgb += (1.0 - accum.a) * color * alpha;
        accum.a += (1.0 - accum.a) * alpha;
    }

    accum *= clamp(vOpacity * vf.render.w, 0.0, 1.0);
    if (accum.a <= 0.002)
        discard;
    outColor = accum;
}
