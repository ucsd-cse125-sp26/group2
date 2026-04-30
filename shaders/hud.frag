/// @file hud.frag
/// @brief HUD fragment shader — branches on texMode for solid, SDF text,
///        sprite, or SDF rounded rect.
#version 450

layout(location = 0) in vec2  vUV;
layout(location = 1) in vec4  vColor;
layout(location = 2) in float vTexMode;
layout(location = 3) in vec3  vShapeData;

layout(set = 2, binding = 0) uniform sampler2D sdfAtlas;
layout(set = 2, binding = 1) uniform sampler2D iconAtlas;

layout(location = 0) out vec4 outColor;

void main()
{
    int mode = int(vTexMode + 0.5);

    if (mode == 1) {
        // SDF text with dark outline for readability.
        float sdf = texture(sdfAtlas, vUV).r;

        // fwidth(sdf) gives the correct isotropic AA width for SDF edges.
        // Clamp to prevent too-thin text at small sizes or too-soft at large.
        float fw = clamp(fwidth(sdf), 0.01, 0.12);

        // Fill (glyph interior).
        float fillAlpha = smoothstep(0.5 - fw, 0.5 + fw, sdf);

        // Outline band just outside the glyph edge.
        const float kOutlineWidth = 0.08;
        float outlineEdge  = 0.5 - kOutlineWidth;
        float outlineAlpha = smoothstep(outlineEdge - fw, outlineEdge + fw, sdf);

        // Composite: dark outline behind colored fill.
        vec3  color = mix(vec3(0.0), vColor.rgb, fillAlpha);
        float alpha = outlineAlpha * vColor.a;
        outColor = vec4(color, alpha);

    } else if (mode == 2) {
        // Sprite / icon
        vec4 texel = texture(iconAtlas, vUV);
        outColor = texel * vColor;

    } else if (mode == 3) {
        // SDF rounded rectangle.
        // vUV = local position within quad, [0,1]² → map to [-1,1]²
        // vShapeData = (halfWidth, halfHeight, cornerRadius) in pixels.
        vec2  halfExt = vShapeData.xy;
        float radius  = vShapeData.z;

        // Map UV to pixel offset from center.
        vec2 localPos = (vUV * 2.0 - 1.0) * halfExt;

        // Signed distance to rounded rectangle.
        vec2 d = abs(localPos) - (halfExt - radius);
        float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;

        // Anti-aliased edge.
        float fw    = fwidth(dist);
        float alpha = 1.0 - smoothstep(-fw, fw, dist);
        outColor = vec4(vColor.rgb, vColor.a * alpha);

    } else if (mode == 4) {
        // Vignette: radial edge gradient.
        // UV covers full screen [0,1]².  Color is the vignette tint; alpha
        // controls overall intensity.  The effect fades from fully transparent
        // at the center to full color at the edges/corners.
        vec2 uv = vUV * 2.0 - 1.0;            // remap to [-1,1]
        float dist = length(uv);               // 0 at center, ~1.4 at corners
        float vig = smoothstep(0.35, 1.4, dist); // transparent center, opaque edges
        vig = vig * vig;                        // sharpen the falloff
        outColor = vec4(vColor.rgb, vColor.a * vig);

    } else {
        // Mode 0: solid color
        outColor = vColor;
    }
}
