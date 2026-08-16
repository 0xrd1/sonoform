#pragma once

// C++ mirrors of the std430 structs declared in assets/shaders/common/*.glsl.
// Field order and size are load-bearing: these are memcpy'd straight into
// SSBOs, so they must stay byte-for-byte compatible with their GLSL
// counterparts. Everything is expressed in vec4s specifically to sidestep
// std430's vec3-padding rules (a vec3 member is padded to 16 bytes anyway,
// so packing to vec4 up front avoids silently-wrong offsets).

struct GpuVec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

// Mirrors `struct Particle` in common/particle_types.glsl.
struct GpuParticle {
    GpuVec4 positionLife;  // xyz = world position, w = life remaining (seconds)
    GpuVec4 velocitySize;  // xyz = velocity, w = sprite size (world-space radius)
    GpuVec4 color;         // rgba, straight alpha
    GpuVec4 params;        // x = maxLife, y = rng seed, zw = reserved
};
static_assert(sizeof(GpuParticle) == 64, "GpuParticle must match the GLSL std430 Particle layout exactly");

// Mirrors `struct ForceDesc` in common/forces.glsl. See that file for the
// per-type meaning of `a` and `b`.
enum class GpuForceType {
    GravityWell = 0,
    Drag = 1,
    Vortex = 2,
    Turbulence = 3,
    Directional = 4,
    ShapeConform = 5,
};

struct GpuForceDesc {
    GpuVec4 typeAndParams;
    GpuVec4 a;
    GpuVec4 b;
};
static_assert(sizeof(GpuForceDesc) == 48, "GpuForceDesc must match the GLSL std430 ForceDesc layout exactly");
