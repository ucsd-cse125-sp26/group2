#version 450

layout(location = 0) in  float vEdge;   // 0 = centerline, ±1 = outer edge
layout(location = 1) in  vec4  vColor;
layout(location = 0) out vec4  outColor;

void main()
{
    float t = 1.0 - abs(vEdge);

    // Two-layer glow: a wide soft halo + a tight bright core
    float halo  = pow(t, 3.0);          // broader energy glow
    float core  = pow(t, 18.0);         // very tight centerline
    float spike = pow(t, 60.0);         // sub-pixel hot white spike

    // Base color is the arc color (cyan/blue passed from CPU)
    vec4 col = vColor * halo;

    // Add a bright white-blue core that blows out to simulate HDR
    col.rgb += vec3(0.6, 0.85, 1.0) * core  * 2.5;
    col.rgb += vec3(1.0, 1.0,  1.0) * spike * 4.0;
    col.a    = max(col.a, halo);

    outColor = col;
}
