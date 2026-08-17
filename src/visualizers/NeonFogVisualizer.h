#pragma once
#include <memory>
#include "Visualizer.h"
#include "ShapeFogEmitter.h"
#include "ShapeField.h"
#include "ProceduralShapes.h"
#include "LightningSystem.h"
#include "AudioAnalyzer.h"
#include "VoidFloor.h"
#include "EngineSettings.h"

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
//     target a fraction of the fog (see ShapeSettings::recruitFraction) can
//     be attracted to and flow around (ShapeConform force), so structure is
//     *suggested*, not a rigid point cloud -- this is the extension point
//     a real face-tracked model plugs into later, via a future
//     MeshShapeProvider filling the same field. That recruited fraction
//     also gets a long life (see GpuEmitParams::recruitFraction) so it
//     *persists* across shape changes rather than dying and being
//     replaced -- the same particles visibly flow from the old
//     silhouette to the new one, which is what makes this read as one
//     volume morphing instead of a population turning over.
//   - LightningSystem: fractal bolts that fire on intense music, drawn
//     additively (bolts genuinely are light-emitting) and contributing
//     their own transient lights alongside the core light.
//   - VoidFloor: a dark, neutral "stage" (floor, faint grid, contact
//     shadow) so the fog reads as a character occupying real 3D space,
//     not a sprite cloud on flat black -- see gfx/VoidFloor.h. Its color
//     is deliberately plain, not part of the fog's own palette.
// The ShapeField's gradient also doubles as a cheap surface normal for a
// fake volumetric self-shadow (GpuParticleSystem::SetShading, sampled
// per-particle in particle_sim.comp), kept subtle (see
// FogLightingSettings::shadeAmbientFloor) so it reads as a gentle sense of
// form, not external lighting -- lightingSettings_.overheadLightPos
// supplies this angle and a floor-highlight center, but is deliberately
// *not* a real light on the particles (never added to Draw()'s lights[]
// array): every particle's color should read as coming from the core
// light, from within, combined with the noise-broken sprite alpha
// (particle_render.frag) that keeps it reading as a lit volume rather than
// a field of flat, uniform discs.
// Palette is cool blue/white (Tron Legacy), not the earlier violet.
// Audio drives lighting only: the core light's color/intensity and
// lightning both pulse with musical swells. Shape attraction is
// deliberately independent of audio -- morphStrength_ eases toward
// shapeSettings_.morphForce, a standalone driver (user-tunable via '-'/'='
// or the settings panel, see AdjustPrimary) that has nothing to do with
// the music, so the shape-conform pipeline can be proven out (and later
// driven by other external forces, e.g. wind/turbulence) without audio in
// the loop.
//
// Every tunable that used to be a `constexpr` in the .cpp's anonymous
// namespace now lives in one of the ui::*Settings structs below (see
// src/ui/EngineSettings.h) and is exposed live through VisitSettings() to
// the runtime settings panel -- see src/ui/EngineUi.h.
class NeonFogVisualizer : public Visualizer {
public:
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) override;
    void Update(const FrameContext& frame) override;
    void Draw(const RenderContext& ctx) const override;
    const char* Name() const override { return "Neon Fog"; }
    int ParticleCount() const override { return fog_.AliveCountApprox(); }

    const char* ExtraStatusLine() const override;
    const char* DebugInfoText() const override;
    void SecondaryAction() override { CycleShapePreset(); } // bound to a dedicated key in App
    void TertiaryAction() override { shapeSettings_.autoCycle = !shapeSettings_.autoCycle; } // bound to 'M' in App
    void AdjustPrimary(float delta) override; // bound to '-'/'=' in App: nudges shapeSettings_.morphForce

    void VisitSettings(ui::IParamVisitor& v) override;

private:
    void CycleShapePreset();

    // The shared target shape (ShapeField + the analytic provider that
    // bakes into it) -- visualizer-level, since multiple emitters would
    // all be attracted to the same one. fog_ is the one particle
    // population attracted to it today; a second population is a second
    // ShapeFogEmitter member plus three more call sites (Init/Update/Draw)
    // and one more VisitSettings group -- see ShapeFogEmitter.h.
    std::unique_ptr<ShapeField> shapeField_;
    std::unique_ptr<ProceduralShapeProvider> shapeProvider_;
    ShapeFogEmitter fog_;
    LightningSystem lightning_;
    VoidFloor floor_;

    // All user-tunable state that's genuinely visualizer-level -- see
    // src/ui/EngineSettings.h for field-level docs and tooltips (the same
    // ParamMeta text shown in the panel). fog_'s own emission/force/
    // attraction settings live on fog_ itself (see ShapeFogEmitter.h).
    ui::ShapeSettings shapeSettings_;
    ui::FogLightingSettings lightingSettings_;
    ui::LightningSettings lightningSettings_;
    VoidFloor::Params floorParams_;
    // Seeds floorParams_'s starting look once, the first time Init() runs
    // (see Init()'s definition) -- Init() re-runs on "Rebuild Systems"
    // without this guard, that re-seed would silently discard any floor
    // tuning the user had already dialed in.
    bool settingsSeeded_ = false;

    float morphStrength_ = 0.0f; // smoothed toward shapeSettings_.morphForce each frame

    // Auto-advances through shape presets on a fixed timer so the morph
    // is visible without user input; 'S' can also force the next shape
    // immediately, and 'M' (or the panel) toggles this on/off.
    float shapeTimer_ = 0.0f;

    float beatFlash_ = 0.0f;
    float lightningCooldown_ = 0.0f;

    // Cached from FrameContext::time each Update(), for Draw() (const, no
    // FrameContext of its own) to feed particle_render.frag's sprite-noise
    // time drift and, in principle, anything else Draw() later needs the
    // current time for.
    float lastTime_ = 0.0f;

    // The fog's primary light source, updated in Update() (audio-driven
    // color/intensity) and read in Draw() (const) -- see the class
    // comment on why lighting, not particle albedo, carries the color.
    Color coreLightColor_ = WHITE;
    float coreLightIntensity_ = 0.0f;
};
