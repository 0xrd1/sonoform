#version 430

#include "../common/particle_types.glsl"

uniform mat4 uViewProj;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uSizeScale;  // global size multiplier (e.g. half-res render target compensation)
uniform int uFadeMode;     // 0 = linear, 1 = eased (min(1, ratio*1.5)), 2 = two-sided edge fade

// Point lights (e.g. active lightning bolts) that visibly brighten nearby
// particles -- see gfx/LightSample.h. Explicitly set to uLightCount=0 by
// every GpuParticleSystem::Draw call that doesn't pass lights, so a
// visualizer that used lighting last frame can never leak stale light
// state into a different visualizer's draw (GLSL uniforms persist across
// draw calls on the same program until overwritten).
#define MAX_LIGHTS 16
uniform vec4 uLightPositions[MAX_LIGHTS]; // xyz = position, w = intensity
uniform vec4 uLightColors[MAX_LIGHTS];    // rgb = color, w unused
uniform int uLightCount;

out vec2 vUv;
out vec4 vColor;
out vec2 vSeedOffset;

// A camera-facing quad (2 triangles, 6 vertices) generated purely from
// gl_VertexID -- no vertex buffer exists. Combined with gl_InstanceID
// pulling particle data from the SSBO, this is "vertex pulling": the only
// per-draw state is an empty bound VAO (see ParticleRenderer::Init).
const vec2 kQuadOffsets[6] = vec2[6](
    vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
    vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5)
);

// Fade curves ported verbatim from each visualizer's old CPU Draw():
// mode 1 was GalaxyVisualizer's Fade(color, min(1, lifeRatio*1.5)); mode 2
// was TunnelVisualizer's two-sided fade so particles don't pop at spawn or
// despawn. Selected per-draw via a uniform rather than duplicated shaders.
float FadeCurve(float lifeRatio) {
    if (uFadeMode == 1) return min(1.0, lifeRatio * 1.5);
    if (uFadeMode == 2) return min(1.0, (1.0 - lifeRatio) * 4.0) * min(1.0, lifeRatio * 6.0);
    return lifeRatio;
}

void main() {
    Particle p = particles[gl_InstanceID];
    float life = p.positionLife.w;

    if (life <= 0.0) {
        // Dead: collapse to a degenerate triangle so it rasterizes nothing.
        gl_Position = vec4(0.0);
        vUv = vec2(0.0);
        vColor = vec4(0.0);
        vSeedOffset = vec2(0.0);
        return;
    }

    float maxLife = max(p.params.x, 0.0001);
    float lifeRatio = clamp(life / maxLife, 0.0, 1.0);

    vec2 corner = kQuadOffsets[gl_VertexID];
    float size = p.velocitySize.w * uSizeScale;
    vec3 worldPos = p.positionLife.xyz + (uCameraRight * corner.x + uCameraUp * corner.y) * size;

    // Additively boost color from nearby point lights (e.g. lightning
    // bolts) -- left unclamped deliberately, since additive blending
    // (GL_SRC_ALPHA, GL_ONE) plus bloom means a bright boost naturally
    // reads as the fog being lit from within, up to clipping toward
    // white at the most intense strikes, which looks correct here.
    vec3 lightBoost = vec3(0.0);
    for (int i = 0; i < uLightCount; i++) {
        vec3 toLight = uLightPositions[i].xyz - worldPos;
        // The "+ 1.0" floor on the denominator (beyond the usual "+1"
        // that just avoids divide-by-zero) caps falloff at intensity*0.5
        // even for a particle sitting exactly at the light's position --
        // without it, dense fog concentrated near a bright light (e.g.
        // gravity pulling mass toward the core light's position) blows
        // out to solid white well before it reads as "brightly lit".
        float falloff = uLightPositions[i].w / (2.0 + dot(toLight, toLight) * 0.3);
        lightBoost += uLightColors[i].rgb * falloff;
    }

    // Fake volumetric self-shadow: a light-facing factor baked into
    // params.w by particle_sim.comp from the ShapeField's gradient (acting
    // as a surface normal) when this system has one bound -- see the
    // uHasShapeField block there. Defaults to 1.0 (no darkening) for
    // particles that never get it computed, so this is a pure no-op for
    // any system without a bound ShapeField. Applied only to the
    // particle's own base color, not lightBoost, which already models
    // real point lights independently.
    float shade = p.params.w;

    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vUv = corner + 0.5;
    // Per-particle offset into the noise field used by particle_render.frag
    // to break up the sprite's silhouette -- derived from the particle's
    // own rng seed so neighboring particles don't show an identical
    // pattern (which would look like a tiled texture instead of fog).
    vSeedOffset = vec2(fract(p.params.y * 0.1031), fract(p.params.y * 0.2947)) * 37.0;
    vColor = vec4(p.color.rgb * shade + lightBoost, p.color.a * FadeCurve(lifeRatio));
}
