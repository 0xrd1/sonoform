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
// morphStrength (how strongly the recruited fraction is pulled into the
// current shape) and the core light's color/intensity are both audio-
// driven, so structure and color both pulse with musical swells.
class NeonFogVisualizer : public Visualizer {
public:
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) override;
    void Update(const FrameContext& frame) override;
    void Draw(const RenderContext& ctx) const override;
    const char* Name() const override { return "Neon Fog"; }
    int ParticleCount() const override { return fog_ ? fog_->AliveCountApprox() : 0; }

    const char* ExtraStatusLine() const override;
    void SecondaryAction() override { CycleShapePreset(); } // bound to a dedicated key in App

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
    bool shapeEnabled_ = true;
    float morphStrength_ = 0.0f; // smoothed toward morphTarget_ each frame
    float morphTarget_ = 0.0f;

    float spawnAccumulator_ = 0.0f;
    float beatFlash_ = 0.0f;
    float lightningCooldown_ = 0.0f;

    // The fog's primary light source, updated in Update() (audio-driven
    // color/intensity) and read in Draw() (const) -- see the class
    // comment on why lighting, not particle albedo, carries the color.
    Color coreLightColor_ = WHITE;
    float coreLightIntensity_ = 0.0f;
};
