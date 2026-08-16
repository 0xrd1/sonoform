#pragma once
#include "raylib.h"

// A point light contributed to particle rendering (see
// GpuParticleSystem::Draw / ParticleRenderer::Draw's light parameters).
// Shared between GpuParticleSystem (which uploads these as uniforms) and
// LightningSystem (which produces them from active bolts), so fog
// particles near a lightning strike visibly brighten -- see
// particle_render.vert's light-boost loop.
struct LightSample {
    Vector3 position{ 0, 0, 0 };
    float intensity = 1.0f;
    Color color = WHITE;
};

constexpr int kMaxParticleLights = 16; // must match MAX_LIGHTS in particle_render.vert
