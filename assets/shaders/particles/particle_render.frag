#version 430

in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;

void main() {
    // Analytic soft-round sprite -- no texture needed. Equivalent to the
    // old CPU path's procedurally generated radial-gradient texture
    // (GenImageGradientRadial in App.cpp), just computed per-fragment.
    vec2 centered = vUv * 2.0 - 1.0;
    float d = length(centered);
    // smoothstep requires edge0 < edge1 (GLSL spec: undefined otherwise);
    // fade-out-with-distance is edge0=0 (opaque) -> edge1=1 (transparent),
    // so compute it that way and invert, rather than smoothstep(1,0,d).
    float alpha = 1.0 - smoothstep(0.0, 1.0, d);

    // Fragments outside the circular sprite are fully transparent by
    // construction (the analytic falloff above), but the quad itself
    // still covers a full square -- discard those corner fragments
    // entirely rather than blending in a zero-alpha color, so they never
    // interact with depth (see ParticleRenderer::Draw's depth-mask
    // comment for why that matters) or cost a blend for nothing.
    if (alpha < 0.02) discard;

    // Additive blending (glBlendFunc(GL_SRC_ALPHA, GL_ONE), see
    // rlgl.h's RL_BLEND_ADDITIVE) already multiplies rgb by alpha in the
    // fixed-function blend stage -- do not premultiply here too, or
    // faint particles dim by alpha^2 instead of alpha.
    fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
