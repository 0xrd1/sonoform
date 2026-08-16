#pragma once

// Shared SSBO binding-point constants. Must exactly match the
// `layout(std430, binding = N)` declarations in assets/shaders/common/*.glsl
// -- this header is the single source of truth for both sides; if you add
// a new binding, add it here first and reference the number in the shader.
namespace gpu_bindings {

constexpr unsigned int kParticleBuffer = 0;  // common/particle_types.glsl
constexpr unsigned int kFreeListBuffer = 1;  // common/particle_types.glsl
constexpr unsigned int kForceBuffer = 2;     // common/forces.glsl
constexpr unsigned int kShapeFieldBuffer = 3;// shapes/shape_field.glsl (phase 5)
constexpr unsigned int kLightBuffer = 4;     // lightning/lights.glsl (phase 7)

} // namespace gpu_bindings
