#pragma once
#include "raylib.h"

// Per-particle color tint driven live in particle_render.vert from each
// particle's own world position -- see FogColorSettings in EngineSettings.h
// for the tunable fields this mirrors, and NeonFogVisualizer::Draw for how
// it's filled from those settings plus the current audio levels. A plain
// struct passed by const reference into ParticleRenderer::Draw, the same
// pattern LightSample.h already uses, rather than another half-dozen
// arguments tacked onto that call's signature.
//
// Deliberately computed from worldPos every frame, not baked into a
// particle at spawn time (there's a free params.z slot that could have
// held it): the fog body morphs and drifts continuously, so a spawn-time
// coordinate would smear as particles moved away from where they were
// tinted. Sampling live keeps the palette locked to the *current* shape,
// like a light gel rather than a paint job.
enum class PaletteMode : int {
    Off = 0,
    Height = 1,  // t = normalized Y offset from center
    Radius = 2,  // t = normalized distance from center
    Angle = 3,   // t = azimuthal angle around center, mapped to [0,1]
    Random = 4,  // t = per-particle hash of the existing rng seed (params.y)
};

struct PaletteParams {
    PaletteMode mode = PaletteMode::Off;
    Vector3 center{ 0.0f, 0.0f, 0.0f };
    // World-space distance at which Height/Radius modes reach t=1 --
    // typically the shape's own half-extent, so the ramp spans exactly the
    // visible body regardless of its current scale.
    float extent = 4.0f;

    float hueA = 0.0f;   // degrees
    float hueB = 240.0f; // degrees
    float saturation = 0.8f;

    // How strongly the tint mixes into particle albedo (0 = untinted) and
    // into the light response (0 = the light stays its own color) -- see
    // particle_render.vert's comment on why both exist: albedo alone is
    // too dim to read (~0.1-0.28 value vs. lightBoost's 3-9), so the light
    // term is what actually makes the palette visible.
    float strength = 0.0f;
    float lightTint = 0.0f;
};
