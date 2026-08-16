// Shared particle data layout used by every particle compute/render shader.
// Must stay binary-compatible with the C++ GpuParticle POD in
// src/particles/ParticleTypes.h (std430, 4x vec4 = 64 bytes/particle).
//
// Buffer binding points (must match GpuBindings.h on the C++ side):
//   0 = ParticleBuffer   1 = FreeListBuffer   2 = ForceBuffer
//   3 = ShapeFieldBuffer 4 = LightBuffer

struct Particle {
    vec4 positionLife;   // xyz = world position, w = life remaining (seconds); <=0 means dead/free
    vec4 velocitySize;   // xyz = velocity, w = sprite size (world-space radius)
    vec4 color;          // rgba, straight alpha; alpha fade-by-life is applied at render time
    vec4 params;         // x = maxLife, y = rng seed, z = emitter id,
                          // w = shade factor (fake volumetric self-shadow;
                          // see particle_sim.comp's uHasShapeField block
                          // and particle_render.vert). Defaults to 1.0 (no
                          // darkening) at spawn -- see particle_emit.comp.
};

layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

// Free-list of dead particle slots. freeCount is an atomic counter;
// freeIndices holds that many valid indices at its front. Sized to
// capacity+1 ints when allocated (see GpuParticleSystem::Init).
layout(std430, binding = 1) buffer FreeListBuffer {
    int freeCount;
    int freeIndices[];
};
