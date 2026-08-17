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

// Real top-down particle shadow -- a picture of where the fog actually is
// this frame (see gfx/VoidFloor.h's ShadowMap and NeonFogVisualizer::PreDraw),
// not the static analytic-shape SDF. Falls back to the uHasShapeField path
// below when uHasShadowMap is 0 (e.g. the very first frame, before any
// top-down pass has run).
uniform int uHasShadowMap;
uniform sampler2D uShadowMapTex;
uniform vec2 uShadowMapCenter;   // world XZ center of the shadow render's ortho frustum
uniform float uShadowMapHalfExtent;

// Same point-light array particles themselves are lit by -- see
// particle_render.vert's identical uniforms and light-boost loop, which
// the loop below mirrors exactly so the floor picks up the same core/
// spectral/lightning colors, not just a static hint.
#define MAX_LIGHTS 16
uniform vec4 uLightPositions[MAX_LIGHTS]; // xyz = position, w = intensity
uniform vec4 uLightColors[MAX_LIGHTS];    // rgb = color, w unused
uniform int uLightCount;

void main() {
    vec2 offset = vWorldPos.xz - uFloorCenter.xz;
    float distFromCenter = length(offset);

    // Fade to fully transparent well before the plane's actual edge, so
    // the floor blends seamlessly into the black-cleared background
    // instead of showing a hard rectangular boundary -- this is what
    // sells "liminal void" rather than "a lit room with walls".
    float voidFade = 1.0 - smoothstep(uVoidRadius * 0.45, uVoidRadius, distFromCenter);
    if (voidFade <= 0.001) discard;

    // Thin, faint grid lines -- neutral grey, not glowing -- the primary
    // depth/scale cue in an otherwise featureless dark plane.
    vec2 g = abs(fract(offset / uGridSpacing) - 0.5) * uGridSpacing;
    float lineDist = min(g.x, g.y);
    float line = 1.0 - smoothstep(0.0, uGridLineWidth, lineDist);

    // Soft brighter pool roughly under the overhead key light.
    float poolDist = length(vWorldPos.xz - uLightPoolCenter.xz);
    float pool = (1.0 - smoothstep(0.0, uLightPoolRadius, poolDist)) * uLightPoolStrength;

    vec3 color = uBaseColor + uGridColor * line + uGridColor * pool * 0.5;

    // Real per-fragment lighting from the same lights[] array particles
    // are lit by -- identical falloff formula to particle_render.vert's
    // lightBoost loop, so the floor picks up their actual color/position,
    // not just a static hint.
    vec3 lightBoost = vec3(0.0);
    for (int i = 0; i < uLightCount; i++) {
        vec3 toLight = uLightPositions[i].xyz - vWorldPos;
        float falloff = uLightPositions[i].w / (2.0 + dot(toLight, toLight) * 0.3);
        lightBoost += uLightColors[i].rgb * falloff;
    }
    // Unlike particles (additive sprites, left unclamped -- see that
    // shader's comment), the floor is a flat opaque surface: a soft
    // fractional weight keeps a bright nearby light from blowing a whole
    // patch of stage out to solid white.
    color += lightBoost * 0.35;

    float shadowFactor = 1.0;
    if (uHasShadowMap != 0) {
        // Real shadow: an actual top-down render of the particle mass this
        // frame (see gfx/VoidFloor.h's ShadowMap) -- moves, swirls, and
        // thins exactly as the fog does, unlike the SDF fallback below.
        vec2 shadowUV = (vWorldPos.xz - uShadowMapCenter) / uShadowMapHalfExtent * 0.5 + 0.5;
        float density = 0.0;
        if (shadowUV.x >= 0.0 && shadowUV.x <= 1.0 && shadowUV.y >= 0.0 && shadowUV.y <= 1.0) {
            density = texture(uShadowMapTex, shadowUV).a;
        }
        shadowFactor = mix(1.0, 1.0 - uShadowStrength, clamp(density, 0.0, 1.0));
    } else if (uHasShapeField != 0) {
        // Fallback only: the old analytic-shape SDF proximity shadow,
        // reached only before NeonFogVisualizer::PreDraw has ever
        // rendered a real shadow map (e.g. the first frame).
        vec3 samplePos = vec3(vWorldPos.x, uShapeSampleY, vWorldPos.z);
        vec4 fieldSample = SampleShapeField(samplePos);
        float proximity = 1.0 - smoothstep(0.0, uShadowRadius, abs(fieldSample.w));
        shadowFactor = mix(1.0, 1.0 - uShadowStrength, proximity);
    }
    color *= shadowFactor;

    fragColor = vec4(color * voidFade, voidFade);
}
