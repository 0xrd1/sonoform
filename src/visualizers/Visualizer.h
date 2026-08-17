#pragma once
#include "raylib.h"

class ShaderLibrary;
class ParticleRenderer;
class AudioAnalyzer;
namespace ui { class IParamVisitor; struct DebugSettings; }

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

    // In-scene debug gizmo toggles (shape bounds, lights, field axes,
    // emitter bounds, force vectors) -- see ui::DebugSettings and
    // NeonFogVisualizer::Draw. Null when the App-level debug window hasn't
    // set one up, so a visualizer's Draw() must null-check before use.
    const ui::DebugSettings* debug = nullptr;
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

    // Optional multi-line numeric readout for the Debug View window (see
    // src/ui/EngineUi.h's DrawDebugWindow) -- for precise values (grid
    // resolution, voxel size, field center, current light color/intensity)
    // that a 3D gizmo communicates poorly. Same rotating-TextFormat-buffer
    // convention as ExtraStatusLine: the returned pointer is only valid
    // until the next TextFormat call, so callers must use it immediately.
    // Return nullptr (the default) to show nothing.
    virtual const char* DebugInfoText() const { return nullptr; }

    // Optional visualizer-specific action bound to a dedicated key (e.g.
    // cycling a shape preset). Default no-op.
    virtual void SecondaryAction() {}

    // Optional second visualizer-specific toggle (e.g. Neon Fog's
    // shape-auto-cycle on/off). Default no-op.
    virtual void TertiaryAction() {}

    // Optional continuous visualizer-specific parameter nudge, e.g. a
    // held key raising/lowering some independent driver value. `delta`
    // carries sign and magnitude (already scaled by dt by the caller).
    // Default no-op.
    virtual void AdjustPrimary(float /*delta*/) {}

    // Exposes every tunable this visualizer owns to the runtime settings
    // panel (see src/ui/EngineUi.h) -- the general-purpose successor to the
    // SecondaryAction/TertiaryAction/AdjustPrimary hooks above, which cap
    // out at exactly one continuous float and two toggles per visualizer.
    // Default no-op, so a visualizer that hasn't been wired up yet (see
    // App.cpp's kAudioEnabled comment on the other four) simply contributes
    // nothing to the panel instead of needing a stub override. Implementors
    // typically wrap each owned settings struct's own Visit() in a
    // v.BeginGroup(name)/v.EndGroup() pair -- see NeonFogVisualizer::VisitSettings.
    virtual void VisitSettings(ui::IParamVisitor& /*v*/) {}
};
