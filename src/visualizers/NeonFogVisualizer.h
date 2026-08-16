#pragma once
#include <memory>
#include "Visualizer.h"
#include "GpuParticleSystem.h"
#include "ShapeField.h"
#include "ProceduralShapes.h"
#include "LightningSystem.h"
#include "AudioAnalyzer.h"

// A dense fog volume lit from within, rather than self-luminous. Real
// smoke/fog VFX is rendered with a neutral (near-grey) particle albedo
// under standard alpha blending, colored only by the light scattering
// through it -- additive blending is for fire/energy/sparks, and applied
// to fog it reads as a glowing gas cloud instead of a lit one. Here that
// means: particles carry almost no color of their own; a persistent,
// audio-reactive "core light" at the structure's center (plus transient
// lightning) does the actual coloring via GpuParticleSystem's per-
// particle light-boost (see particle_render.vert), the same
// nearest-lights/inverse-square-falloff approximation real-time engines
// use in place of full volumetric scattering.
//
// Ties together every system this project added on top of the original
// CPU visualizers:
//   - GpuParticleSystem: the fog body itself (a large, long-lived pool),
//     drawn with alpha blending (not additive -- see above).
//   - ShapeField + ProceduralShapeProvider: an abstract signed-distance
//     target a fraction of the fog can be attracted to and flow around
//     (ShapeConform force), so structure is *suggested*, not a rigid
//     point cloud -- this is the extension point a real face-tracked
//     model plugs into later, via a future MeshShapeProvider filling the
//     same field. Particles pulled onto the shape sit closer to the core
//     light and so read brighter than the ambient fog around them,
//     which is what actually makes the structure legible.
//   - LightningSystem: fractal bolts that fire on intense music, drawn
//     additively (bolts genuinely are light-emitting) and contributing
//     their own transient lights alongside the core light.
// Audio drives lighting only: the core light's color/intensity and
// lightning both pulse with musical swells. Shape attraction is
// deliberately independent of audio -- morphStrength eases toward
// morphForce_, a standalone driver (user-tunable via '-'/'=', see
// AdjustPrimary) that has nothing to do with the music, so the
// shape-conform pipeline can be proven out (and later driven by other
// external forces, e.g. wind/turbulence) without audio in the loop.
class NeonFogVisualizer : public Visualizer {
public:
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) override;
    void Update(const FrameContext& frame) override;
    void Draw(const RenderContext& ctx) const override;
    const char* Name() const override { return "Neon Fog"; }
    int ParticleCount() const override { return fog_ ? fog_->AliveCountApprox() : 0; }

    const char* ExtraStatusLine() const override;
    void SecondaryAction() override { CycleShapePreset(); } // bound to a dedicated key in App
    void TertiaryAction() override { autoCycle_ = !autoCycle_; } // bound to 'M' in App
    void AdjustPrimary(float delta) override; // bound to '-'/'=' in App: nudges morphForce_

private:
    void CycleShapePreset();

    std::unique_ptr<GpuParticleSystem> fog_;
    std::unique_ptr<ShapeField> shapeField_;
    std::unique_ptr<ProceduralShapeProvider> shapeProvider_;
    LightningSystem lightning_;

    int gravityForceIndex_ = -1;
    int turbulenceForceIndex_ = -1;
    int shapeConformForceIndex_ = -1;

    Vector3 fieldCenter_{ 0, 2.0f, 0 };
    float morphStrength_ = 0.0f; // smoothed toward morphTarget_ (== morphForce_) each frame
    float morphTarget_ = 0.0f;

    // The independent driver behind shape attraction -- deliberately not
    // derived from audio (see the class comment: audio drives lighting
    // only). User-tunable via '-'/'=' (down to 0, which dissolves all
    // structure back into uniform fog) so the attraction pipeline is
    // demonstrably decoupled from the music.
    float morphForce_ = 1.0f;

    // Auto-advances through shape presets on a fixed timer so the morph
    // is visible without user input; 'S' can also force the next shape
    // immediately, and 'M' toggles this on/off.
    bool autoCycle_ = true;
    float shapeTimer_ = 0.0f;

    float spawnAccumulator_ = 0.0f;
    float beatFlash_ = 0.0f;
    float lightningCooldown_ = 0.0f;

    // The fog's primary light source, updated in Update() (audio-driven
    // color/intensity) and read in Draw() (const) -- see the class
    // comment on why lighting, not particle albedo, carries the color.
    Color coreLightColor_ = WHITE;
    float coreLightIntensity_ = 0.0f;
};
