#version 430

// A minimal stylized "stage" for the fog to occupy: a dark floor with a
// soft glowing procedural grid (a depth/scale cue against an otherwise
// pure-black void, fading out before any hard edge of the floor plane
// would be visible) and a soft contact shadow sampled directly from the
// same ShapeField the fog's ShapeConform force uses -- not real shadow
// mapping, a cheap stylized stand-in whose silhouette matches whatever
// primitive the fog currently is. See gfx/VoidFloor.h.
#include "../shapes/shape_field_sample.glsl"

in vec3 vWorldPos;
out vec4 fragColor;

uniform vec3 uFloorCenter;
uniform float uVoidRadius;
uniform vec3 uBaseColor;
uniform vec3 uGridColor;
uniform float uGridSpacing;
uniform float uGridLineWidth;

uniform int uHasShapeField;
uniform float uShapeSampleY;
uniform float uShadowRadius;
uniform float uShadowStrength;

uniform vec3 uLightPoolCenter;
uniform float uLightPoolRadius;
uniform float uLightPoolStrength;

void main() {
    vec2 offset = vWorldPos.xz - uFloorCenter.xz;
    float distFromCenter = length(offset);

    // Fade to fully transparent well before the plane's actual edge, so
    // the floor blends seamlessly into the black-cleared background
    // instead of showing a hard rectangular boundary -- this is what
    // sells "liminal void" rather than "a lit room with walls".
    float voidFade = 1.0 - smoothstep(uVoidRadius * 0.45, uVoidRadius, distFromCenter);
    if (voidFade <= 0.001) discard;

    // Thin glowing grid lines (Tron-style), the primary depth/scale cue
    // in an otherwise featureless dark plane.
    vec2 g = abs(fract(offset / uGridSpacing) - 0.5) * uGridSpacing;
    float lineDist = min(g.x, g.y);
    float line = 1.0 - smoothstep(0.0, uGridLineWidth, lineDist);

    // Soft brighter pool roughly under the overhead key light.
    float poolDist = length(vWorldPos.xz - uLightPoolCenter.xz);
    float pool = (1.0 - smoothstep(0.0, uLightPoolRadius, poolDist)) * uLightPoolStrength;

    vec3 color = uBaseColor + uGridColor * line + uGridColor * pool * 0.5;

    if (uHasShapeField != 0) {
        // Top-down projection: sample the field at this floor point's XZ,
        // at the shape's own vertical center -- a cheap orthographic
        // "contact shadow" whose silhouette naturally matches the current
        // shape (sphere/box/torus/cylinder) without any real shadow pass.
        vec3 samplePos = vec3(vWorldPos.x, uShapeSampleY, vWorldPos.z);
        vec4 fieldSample = SampleShapeField(samplePos);
        float proximity = 1.0 - smoothstep(0.0, uShadowRadius, abs(fieldSample.w));
        float shadowFactor = mix(1.0, 1.0 - uShadowStrength, proximity);
        color *= shadowFactor;
    }

    fragColor = vec4(color * voidFade, voidFade);
}
