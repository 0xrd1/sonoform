#pragma once
#include "raylib.h"

class ShaderLibrary;
class ParticleRenderer;
class AudioAnalyzer;

// Everything a visualizer needs to advance its simulation for one frame.
struct FrameContext {
    float dt;
    float time;
    const AudioAnalyzer& audio;
    float intensity;
};

// Everything a visualizer needs to draw itself, computed once per frame by
// App so every visualizer shares the same camera basis / view-projection
// matrix instead of re-deriving it.
struct RenderContext {
    Camera3D camera;
    Matrix viewProj;
    Vector3 cameraRight;
    Vector3 cameraUp;

    // A small procedural soft-glow texture (see App::Init) for the handful
    // of non-GPU-particle accent billboards visualizers still draw
    // directly via raylib's DrawBillboard (e.g. Galaxy's core glow,
    // Fireworks' shell markers) -- everything that's actually part of a
    // particle *system* renders through GpuParticleSystem::Draw instead.
    Texture2D accentTexture;
};

// One visual "scene": owns its own GPU particle system(s) and forces,
// reacts to the AudioAnalyzer each frame, and draws itself via instanced
// GPU rendering. All particle simulation lives on the GPU: Update()
// dispatches compute shaders that mutate SSBO state; Draw() only binds
// already-populated buffers and issues draw calls, so -- like the old
// CPU-backed version -- it stays const.
class Visualizer {
public:
    virtual ~Visualizer() = default;

    // Called once, before first Update/Draw. Visualizers construct their
    // GpuParticleSystem(s) here, which need long-lived references to the
    // shared shader library and particle renderer.
    virtual void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) = 0;

    virtual void Update(const FrameContext& frame) = 0;

    // Called inside BeginMode3D/EndMode3D with additive blending active.
    virtual void Draw(const RenderContext& ctx) const = 0;

    virtual const char* Name() const = 0;

    // Approximate: backed by a throttled GPU->CPU readback, not an exact
    // per-frame count. See GpuParticleSystem::AliveCountApprox.
    virtual int ParticleCount() const = 0;

    // Optional single extra HUD line (e.g. Neon Fog's current shape
    // preset). Return nullptr (the default) to show nothing.
    virtual const char* ExtraStatusLine() const { return nullptr; }

    // Optional visualizer-specific action bound to a dedicated key (e.g.
    // cycling a shape preset). Default no-op.
    virtual void SecondaryAction() {}
};
