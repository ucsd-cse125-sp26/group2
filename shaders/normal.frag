// normal.frag — Lambertian diffuse lighting for the hard-coded scene geometry.
#version 450

layout(location = 0) in  vec3       fragColor;
layout(location = 1) in  vec3       fragWorldPos;
layout(location = 2) in  float      fragIsFloor;
layout(location = 3) flat in vec3   fragNormal;

layout(location = 0) out vec4 outColor;

// Directional light — same direction as the graphics team's geometry.frag
// so scene and model lighting match visually.
const vec3  lightDir   = normalize(vec3(-1.0, -1.0, -1.0));
const vec3  lightColor = vec3(1.0);
const float ambient    = 0.15;

void main()
{
    // Lambertian diffuse: cosine of angle between normal and light direction.
    float NdotL    = max(dot(fragNormal, -lightDir), 0.0);
    float lighting = ambient + NdotL * (1.0 - ambient);

    if (fragIsFloor > 0.5) {
        // Checkerboard pattern: 100-unit tiles in world XZ.
        ivec2 cell    = ivec2(floor(fragWorldPos.xz / 100.0));
        float checker = float((cell.x + cell.y) & 1);
        vec3  base    = mix(vec3(0.18), vec3(0.34), checker);
        outColor = vec4(base * lighting, 1.0);
    } else {
        outColor = vec4(fragColor * lighting, 1.0);
    }
}
