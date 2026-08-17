// Force descriptors uploaded once per frame from GpuParticleSystem's force
// list. Mirrors the old CPU IForce hierarchy (src/particles/Forces.h) but
// as data dispatched through a switch, not virtual calls — the whole point
// of moving simulation onto the GPU.
#include "noise.glsl"
#include "../shapes/shape_conform.glsl"

#define FORCE_GRAVITY_WELL   0
#define FORCE_DRAG           1
#define FORCE_VORTEX         2
#define FORCE_TURBULENCE     3
#define FORCE_DIRECTIONAL    4
#define FORCE_SHAPE_CONFORM  5

// Field meaning per type (kept dense: 2 vec4s of payload per force):
//   GRAVITY_WELL   a.xyz = center            b.x = strength   b.y = softening
//   DRAG           (a unused)                b.x = coefficient
//   VORTEX         a.xyz = center            b.xyz = axis     typeAndParams.y = strength
//   TURBULENCE     (a unused)                b.x = strength   b.y = scale
//   DIRECTIONAL    a.xyz = direction         b.x = strength
//   SHAPE_CONFORM  b.x = attraction strength b.y = curl-flow strength  b.z = morphStrength
//                  b.w = recruit fraction    a.x = volume depth        a.y = flow noise scale
//                  (see ApplyShapeConform in shapes/shape_conform.glsl). Every
//                  particle system's sim shader can express this force, but
//                  only ones that actually add a SHAPE_CONFORM ForceDesc (the
//                  Neon Fog visualizer) ever sample the ShapeField buffer --
//                  it's always declared here so there's one canonical sim
//                  shader rather than a near-duplicate "shaped" variant.
struct ForceDesc {
    vec4 typeAndParams; // x = type (int-valued float), y = secondary scalar (e.g. vortex strength), zw reserved
    vec4 a;
    vec4 b;
};

layout(std430, binding = 2) buffer ForceBuffer {
    ForceDesc forces[];
};

// Applies every non-shape force in array order and returns the accumulated
// acceleration. Drag is the one exception: it mutates `vel` in place
// immediately (using the pre-integration velocity), exactly mirroring
// Forces.cpp's Drag::Apply, which rewrites p.velocity mid-loop rather than
// contributing to acceleration.
vec3 ApplyForces(vec3 pos, inout vec3 vel, float dt, float time, uint forceCount, float particleSeed) {
    vec3 accel = vec3(0.0);

    for (uint i = 0u; i < forceCount; i++) {
        ForceDesc f = forces[i];
        int type = int(f.typeAndParams.x);

        if (type == FORCE_GRAVITY_WELL) {
            vec3 toCenter = f.a.xyz - pos;
            float distSq = dot(toCenter, toCenter) + f.b.y * f.b.y;
            float dist = sqrt(distSq);
            accel += (toCenter / dist) * (f.b.x / distSq);
        } else if (type == FORCE_DRAG) {
            vel -= vel * f.b.x * dt;
        } else if (type == FORCE_VORTEX) {
            vec3 axis = normalize(f.b.xyz);
            vec3 toParticle = pos - f.a.xyz;
            vec3 radial = toParticle - axis * dot(toParticle, axis);
            float radius = length(radial);
            if (radius > 0.001) {
                vec3 tangent = normalize(cross(axis, radial));
                float falloff = 1.0 / (1.0 + radius * 0.15);
                accel += tangent * (f.typeAndParams.y * falloff);
            }
        } else if (type == FORCE_TURBULENCE) {
            accel += CurlNoise3D(pos * f.b.y, time * 0.3) * f.b.x;
        } else if (type == FORCE_DIRECTIONAL) {
            accel += normalize(f.a.xyz) * f.b.x;
        } else if (type == FORCE_SHAPE_CONFORM) {
            accel += ApplyShapeConform(pos, f.b.x, f.b.y, f.b.z, f.b.w, f.a.x, f.a.y, time, particleSeed);
        }
    }

    return accel;
}
