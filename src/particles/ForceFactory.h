#pragma once
#include "raylib.h"
#include "ParticleTypes.h"

// Convenience constructors for GpuForceDesc, one per force type, so
// visualizer code reads like the old CPU force construction
// (`std::make_shared<GravityWell>()` + named fields) instead of hand-
// packing vec4s. See assets/shaders/common/forces.glsl for the exact
// field-packing contract these must match.
namespace gpu_force {

inline GpuForceDesc GravityWell(Vector3 center, float strength, float softening) {
    GpuForceDesc f;
    f.typeAndParams = { static_cast<float>(GpuForceType::GravityWell), 0, 0, 0 };
    f.a = { center.x, center.y, center.z, 0 };
    f.b = { strength, softening, 0, 0 };
    return f;
}

inline GpuForceDesc Drag(float coefficient) {
    GpuForceDesc f;
    f.typeAndParams = { static_cast<float>(GpuForceType::Drag), 0, 0, 0 };
    f.b = { coefficient, 0, 0, 0 };
    return f;
}

inline GpuForceDesc Vortex(Vector3 center, Vector3 axis, float strength) {
    GpuForceDesc f;
    f.typeAndParams = { static_cast<float>(GpuForceType::Vortex), strength, 0, 0 };
    f.a = { center.x, center.y, center.z, 0 };
    f.b = { axis.x, axis.y, axis.z, 0 };
    return f;
}

inline GpuForceDesc Turbulence(float strength, float scale) {
    GpuForceDesc f;
    f.typeAndParams = { static_cast<float>(GpuForceType::Turbulence), 0, 0, 0 };
    f.b = { strength, scale, 0, 0 };
    return f;
}

inline GpuForceDesc Directional(Vector3 direction, float strength) {
    GpuForceDesc f;
    f.typeAndParams = { static_cast<float>(GpuForceType::Directional), 0, 0, 0 };
    f.a = { direction.x, direction.y, direction.z, 0 };
    f.b = { strength, 0, 0, 0 };
    return f;
}

inline GpuForceDesc ShapeConform(float attractionStrength, float curlStrength, float morphStrength,
                                  float recruitFraction = 0.35f, float volumeDepth = 0.0f,
                                  float flowNoiseScale = 0.35f) {
    GpuForceDesc f;
    f.typeAndParams = { static_cast<float>(GpuForceType::ShapeConform), 0, 0, 0 };
    f.a = { volumeDepth, flowNoiseScale, 0, 0 };
    f.b = { attractionStrength, curlStrength, morphStrength, recruitFraction };
    return f;
}

} // namespace gpu_force
