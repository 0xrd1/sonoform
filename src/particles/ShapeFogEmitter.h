#pragma once
#include <memory>
#include "raylib.h"
#include "GpuParticleSystem.h"
#include "EngineSettings.h"

class ShaderLibrary;
class ParticleRenderer;
class ShapeField;
class AudioAnalyzer;
namespace ui { class IParamVisitor; }

// The generic, reusable half of what NeonFogVisualizer used to do
// monolithically: a GPU particle population that spawns on/near a shared
// ShapeField's surface (GpuEmitMode::ShapeSurface) and is held there by a
// ShapeConform force. Owns exactly what's per-population -- the
// GpuParticleSystem, its four force indices, its spawn accumulator, and
// its own emission/force/attraction settings -- and nothing about the
// shared target shape itself (that's ShapeField, owned by the caller) or
// anything visualizer-level (lighting, floor, lightning, the morph driver).
// A second population attracted to the same shape (different color/size/
// behavior) is a second ShapeFogEmitter instance plus three more call
// sites (Init/Update/Draw) and one more VisitSettings group -- no
// duplicated logic.
class ShapeFogEmitter {
public:
    // Constructs the GpuParticleSystem (sized by emission.capacity) and
    // its four forces, and binds `field` for ShapeSurface-mode spawning
    // and ShapeConform sampling. Re-entrant, like GpuParticleSystem's own
    // construction pattern: safe to call again (e.g. from "Rebuild
    // Systems") to reallocate at a new capacity -- the prior
    // GpuParticleSystem is freed by the unique_ptr assignment before the
    // new one is constructed.
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer, ShapeField& field);

    // Advances forces/shading/spawn for one frame. `field` is the shared
    // target this emitter samples -- passed in rather than cached, so it
    // always reflects the caller's authoritative, possibly-just-rebaked
    // ShapeField (see ShapeField::SetCenter/SetHalfExtent). `morphStrength`
    // is the eased shape-attraction driver (owned by the caller). `shadeLightDir`/
    // `shadeAmbientFloor` feed SetShading every frame, so Lighting panel
    // edits apply live. `audioAnalyzer` feeds the `audio` settings below
    // (Turbulence x Excitement/Treble -- see FogAudioSettings) when
    // `audio.reactive`; reads harmless zero-initialized defaults when no
    // track is loaded/playing, same as every other audio.* consumer in
    // this project, so this is always safe to call.
    void Update(float dt, float time, const ShapeField& field, float morphStrength,
                Vector3 shadeLightDir, float shadeAmbientFloor, const AudioAnalyzer& audioAnalyzer);

    // fadeMode is fixed at 2 (two-sided edge fade -- see
    // particle_render.vert's FadeCurve) and sizeScale at 1.0: neither has
    // needed to vary per-call across this project's history, so they're
    // not exposed here; add parameters if a future emitter needs them.
    void Draw(const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
              const LightSample* lights, int lightCount, float time, int spriteStyle,
              const PaletteParams& palette = PaletteParams{}, float alphaScale = 1.0f) const;

    int AliveCountApprox() const { return system_ ? system_->AliveCountApprox() : 0; }

    // One-shot outward velocity kick (see GpuParticleSystem::
    // ApplyRadialImpulse) -- forwarded so callers never need to reach
    // through to the owned GpuParticleSystem directly. Called by
    // NeonFogVisualizer::Update() on a hard beat (see FogAudioSettings::
    // kickBeatThreshold/kickCooldownSeconds) -- the beat-detection/
    // cooldown logic itself lives there, mirroring the existing lightning-
    // trigger pattern, not duplicated here.
    void KickImpulse(Vector3 center, float strength, float maxRadius) {
        if (system_) system_->ApplyRadialImpulse(center, strength, maxRadius);
    }

    // Emits a burst of `audio.kickBurstCount` guaranteed-unrecruited
    // debris particles (recruitFraction 0 -- see GpuEmitParams), scattered
    // across `attraction.candidateHalfExtent` (proportional to the
    // shape's own scale) around `center` with a strong outward velocity
    // (audio.kickBurstSpeed/SpeedJitter) and a short life
    // (audio.kickBurstLife/LifeJitter). Unlike KickImpulse (which nudges
    // the *existing* mass -- recruited particles wobble and get pulled
    // back by Shape Attraction), every particle from this burst is
    // guaranteed to fly out, fade via the existing two-sided fade curve,
    // and despawn -- never resettles on the shape. Called alongside
    // KickImpulse on a hard beat (see NeonFogVisualizer::Update) so the
    // hit reads unambiguously as particles being expelled.
    void EmitImpactBurst(Vector3 center);

    // Wraps emission/force/attraction/audio's own Visit() in named
    // sub-groups -- see EngineSettings.h's FogEmissionSettings/
    // FogForceSettings/FogAttractionSettings/FogAudioSettings. Caller
    // wraps this in its own outer BeginGroup/EndGroup (e.g. per-emitter
    // name), same pattern as NeonFogVisualizer::VisitSettings already
    // uses for Lighting/Lightning/Floor.
    void VisitSettings(ui::IParamVisitor& v);

    ui::FogEmissionSettings emission;
    ui::FogForceSettings force;
    ui::FogAttractionSettings attraction;
    ui::FogAudioSettings audio;

private:
    std::unique_ptr<GpuParticleSystem> system_;

    int gravityForceIndex_ = -1;
    int turbulenceForceIndex_ = -1;
    int dragForceIndex_ = -1;
    int shapeConformForceIndex_ = -1;

    float spawnAccumulator_ = 0.0f;

    // Smoothed audio-band envelopes driving Gravity x Bass / Curl x Mid /
    // Turbulence x Excitement (see FogAudioSettings) -- eased rather than
    // read raw so the forces they drive don't jitter frame-to-frame with
    // every FFT update. Treble stays unsmoothed (read directly in
    // Update()) -- it's deliberately the fast, instantaneous signal.
    float bassSmoothed_ = 0.0f;
    float midSmoothed_ = 0.0f;
    float excitement_ = 0.0f;

    // Independent, much longer low-pass over EnergyLevel() -- see
    // FogAudioSettings::sectionSmoothing's comment. Drives
    // dragAudioScale/shapeAttractionAudioScale: a song-section "mood"
    // envelope, distinct from excitement_ above (which uses the faster
    // motionSmoothing).
    float sectionEnergy_ = 0.0f;
};
