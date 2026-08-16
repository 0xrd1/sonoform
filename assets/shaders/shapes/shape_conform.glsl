// Applies the "conform to shape field" force: attraction along the SDF
// gradient toward the surface, plus a curl-noise-modulated tangential
// flow that keeps particles sliding along the surface instead of
// freezing on it. This is the production VFX technique (Houdini,
// Unity VFX Graph's Conform-to-SDF) for a shape that reads as
// *suggested* by flowing particles rather than a rigid, solid point
// cloud: taking the curl of (SDF-gradient * noise) yields a
// divergence-free velocity field that is, near the surface,
// approximately tangential to it.
#include "shape_field_sample.glsl"
#include "noise.glsl"
#include "../common/recruit.glsl"

// b.x = attraction strength, b.y = curl-flow strength, b.z = morphStrength
// (0 = fully ignore the field -- particles behave as free-floating fog --
// 1 = fully pulled toward/along the shape, for the recruited fraction of
// particles below), b.w = recruit fraction: the share of particles that
// ever respond to shape conforming, regardless of morphStrength. Without
// this, morphStrength=1 pulls literally every particle onto the
// (comparatively small) shape surface at once, which oversaturates into a
// solid blob and reads as a rigid point cloud -- exactly what the VFX
// technique in the module comment above is meant to avoid. Recruiting only
// a subset keeps the rest as permanent ambient fog, so the shape is always
// suggested by a portion of the particles, never all of them. Only a
// ForceDesc of type FORCE_SHAPE_CONFORM invokes this (see forces.glsl);
// other particle systems never call it and never touch the ShapeField
// buffer.
vec3 ApplyShapeConform(vec3 pos, float attractionStrength, float curlStrength, float morphStrength, float recruitFraction, float time, float particleSeed) {
    if (morphStrength <= 0.0001) return vec3(0.0);

    // Deterministic per-particle recruitment from the particle's own rng
    // seed (stable for that particle's whole lifetime -- it doesn't
    // flicker between recruited/not from frame to frame). Shared with
    // particle_emit.comp's spawn-time life decision via RecruitRoll --
    // see common/recruit.glsl -- so a particle spawned with a long
    // ("core") life is guaranteed to also be recruited here, forever.
    if (RecruitRoll(particleSeed) > recruitFraction) return vec3(0.0);

    vec4 fieldSample = SampleShapeField(pos);
    vec3 gradient = fieldSample.xyz;
    float dist = fieldSample.w;

    // Pull toward the surface: outside (dist>0) pulls inward along
    // -gradient, inside (dist<0) pushes outward along +gradient. A small
    // deadband near the surface keeps particles hovering/orbiting it
    // instead of pinning exactly onto it (which would look like a solid
    // shell, not fog) -- kept tight so correction engages almost
    // immediately instead of letting a particle drift noticeably before
    // attraction does anything, which read as "not holding the shape."
    float pull = clamp(abs(dist) - 0.05, 0.0, 4.0);
    vec3 attraction = -sign(dist) * gradient * pull * attractionStrength;

    // Surface-parallel flow.
    float n = ValueNoise3D(pos * 0.6 + vec3(0.0, 0.0, time * 0.15));
    vec3 flow = CurlNoise3D(gradient * n * 3.0, time * 0.2) * curlStrength;

    return (attraction + flow) * morphStrength;
}
