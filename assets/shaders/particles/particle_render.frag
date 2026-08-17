#version 430

#include "../common/noise.glsl"

in vec2 vUv;
in vec4 vColor;
in vec2 vSeedOffset;
out vec4 fragColor;

uniform float uTime;
// 0 = clean circular sprite (baseAlpha only, no mask) -- a debug/comparison
// mode for isolating the noise mask's contribution. 1 (default) = the
// styled wispy look described below. See ui::DebugSettings::disableSpriteNoise
// and GpuParticleSystem::Draw's spriteStyle parameter.
uniform int uSpriteStyle;

void main() {
    // Analytic soft-round base falloff -- equivalent to the old CPU path's
    // procedurally generated radial-gradient texture (GenImageGradientRadial
    // in App.cpp), just computed per-fragment.
    vec2 centered = vUv * 2.0 - 1.0;
    float d = length(centered);
    // smoothstep requires edge0 < edge1 (GLSL spec: undefined otherwise);
    // fade-out-with-distance is edge0=0 (opaque) -> edge1=1 (transparent),
    // so compute it that way and invert, rather than smoothstep(1,0,d).
    float baseAlpha = 1.0 - smoothstep(0.0, 1.0, d);

    float alpha = baseAlpha;
    if (uSpriteStyle != 0) {
        // Break the perfectly circular falloff into an irregular wispy
        // puff: a single soft disc, no matter how small or numerous, still
        // reads as "a circle" once density is high enough to resolve
        // individual sprite edges -- this mask, not raw particle count, is
        // what makes overlapping sprites read as continuous fog texture
        // instead of a field of visible discs. vSeedOffset decorrelates
        // neighboring particles' patterns; slow time drift adds a subtle
        // living shimmer rather than a frozen decal.
        float n = ValueNoise3D(vec3(vUv * 3.2 + vSeedOffset, uTime * 0.12));
        float mask = smoothstep(-0.3, 0.55, n + (1.0 - d) * 0.7);
        alpha *= mask;
    }

    // Fragments outside the sprite are fully transparent by construction,
    // but the quad itself still covers a full square -- discard those
    // corner fragments entirely rather than blending in a zero-alpha
    // color, so they never interact with depth (see ParticleRenderer::
    // Draw's depth-mask comment for why that matters) or cost a blend for
    // nothing.
    if (alpha < 0.02) discard;

    // Additive blending (glBlendFunc(GL_SRC_ALPHA, GL_ONE), see
    // rlgl.h's RL_BLEND_ADDITIVE) already multiplies rgb by alpha in the
    // fixed-function blend stage -- do not premultiply here too, or
    // faint particles dim by alpha^2 instead of alpha.
    fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
