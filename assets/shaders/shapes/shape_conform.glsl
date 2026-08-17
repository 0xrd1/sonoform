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
// suggested by a portion of the particles, never all of them. a.x = volume
// depth, a.y = flow noise scale, a.z = capture range (see their parameters
// below). Only a ForceDesc of type FORCE_SHAPE_CONFORM invokes this (see
// forces.glsl); other particle systems never call it and never touch the
// ShapeField buffer.
//
// volumeDepth: 0 means every recruited particle targets the exact
// zero-surface (dist == 0) -- a hollow lit skin. >0 means each particle
// instead targets its own fixed, randomly-chosen depth inside the shape
// (derived from its seed the same stable way recruitment is, so it never
// changes for that particle's life), spreading the recruited population
// across many nested iso-surfaces so it fills the body instead of
// collapsing onto one shell -- a continuous surface-to-volume control,
// not a separate mode.
//
// captureRange: 0 (the default -- every preset predating this field is
// byte-for-byte unaffected) means attraction has no distance limit at
// all: a recruited particle a hundred units away gets pulled just as hard
// as one right at the surface, since `pull` below is clamped to the same
// max regardless of how far distFromTarget actually is. That's fine for
// fog that's meant to always visibly belong to the shape, but it means a
// particle can never truly be "free" near a shape without being tugged
// toward it -- there's no way to demo "particles drift freely until the
// shape's surface reaches them" with captureRange == 0. >0 fades
// attraction smoothly to exactly zero once |distFromTarget| exceeds this
// range (full strength inside 70% of it, smoothstepped to 0 at the edge),
// so a particle genuinely stops responding until the shape is moved close
// enough -- the literal "particle encounters the SDF bound" behavior.
vec3 ApplyShapeConform(vec3 pos, float attractionStrength, float curlStrength, float morphStrength, float recruitFraction, float volumeDepth, float flowNoiseScale, float captureRange, float time, float particleSeed) {
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

    // Target iso-surface: dist == 0 (the true surface) when volumeDepth
    // is 0, or this particle's own fixed depth inside the shape
    // otherwise -- see volumeDepth's comment above. A different hash
    // constant than RecruitRoll's so the two rolls are decorrelated (a
    // particle's recruitment and its depth-within-the-volume shouldn't
    // move together).
    float depthRoll = fract(particleSeed * 0.7548776662);
    float targetDist = -volumeDepth * depthRoll;
    float distFromTarget = dist - targetDist;

    if (captureRange > 0.0001) {
        float rangeFalloff = 1.0 - smoothstep(captureRange * 0.7, captureRange, abs(distFromTarget));
        if (rangeFalloff <= 0.0001) return vec3(0.0);
        attractionStrength *= rangeFalloff;
        curlStrength *= rangeFalloff; // tangential surface-flow shouldn't apply before the surface is even "encountered" either
    }

    // Pull toward the target: on the far side pulls inward along
    // -gradient, on the near side pushes outward along +gradient. A small
    // deadband near the target keeps particles hovering/orbiting it
    // instead of pinning exactly onto it (which would look like a solid
    // shell, not fog) -- kept tight so correction engages almost
    // immediately instead of letting a particle drift noticeably before
    // attraction does anything, which read as "not holding the shape."
    float pull = clamp(abs(distFromTarget) - 0.05, 0.0, 4.0);
    vec3 attraction = -sign(distFromTarget) * gradient * pull * attractionStrength;

    // Surface-parallel flow: sampled at the particle's own world position
    // (not, as an earlier version did, at a point built from the local
    // surface *normal* -- gradient is nearly identical for every particle
    // sharing the same face/region of the shape, so that version made
    // nearby particles sample nearly the same point in noise-space and
    // inherit nearly identical flow vectors, herding them into shared
    // streamlines instead of dispersing independently, which read as
    // wavy-line/vein clumping). Projected to be tangential to the local
    // surface so it doesn't fight `attraction` above.
    vec3 flow = CurlNoise3D(pos * flowNoiseScale + vec3(0.0, 0.0, time * 0.15), time * 0.2) * curlStrength;
    flow -= gradient * dot(flow, gradient);

    return (attraction + flow) * morphStrength;
}
