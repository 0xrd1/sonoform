#include "ShapeFogEmitter.h"
#include "ForceFactory.h"
#include "ShapeField.h"
#include "ParamVisitor.h"
#include "AudioAnalyzer.h"
#include <algorithm>

void ShapeFogEmitter::Init(ShaderLibrary& shaders, ParticleRenderer& renderer, ShapeField& field) {
    // Re-entrant -- see the header's comment. Every value read below comes
    // from the current emission/force/attraction members, so re-running
    // Init() (e.g. "Rebuild Systems") applies live-edited settings rather
    // than discarding them.
    system_ = std::make_unique<GpuParticleSystem>(shaders, renderer, emission.capacity);
    system_->SetShapeField(&field);

    gravityForceIndex_ = system_->AddForce(gpu_force::GravityWell(field.Center(), force.gravityStrength, force.gravitySoftening));
    turbulenceForceIndex_ = system_->AddForce(gpu_force::Turbulence(force.turbulenceStrength, force.turbulenceScale));
    dragForceIndex_ = system_->AddForce(gpu_force::Drag(force.dragCoefficient));
    shapeConformForceIndex_ = system_->AddForce(
        gpu_force::ShapeConform(force.shapeAttraction, force.shapeCurl, 0.0f, attraction.recruitFraction,
                                 attraction.volumeDepth, force.flowNoiseScale));

    spawnAccumulator_ = 0.0f;
}

void ShapeFogEmitter::Update(float dt, float time, const ShapeField& field, float morphStrength,
                              Vector3 shadeLightDir, float shadeAmbientFloor, const AudioAnalyzer& audioAnalyzer) {
    if (!system_) return;

    // Forces are re-pushed every frame so panel edits apply live -- see
    // ui::FogForceSettings/FogAttractionSettings. Note what audio never
    // touches: shapeAttraction/shapeCurl/morphStrength here are exactly
    // the panel/driver values, un-modulated -- see FogAudioSettings'
    // comment on why the shape-holding forces are structurally exempt
    // from audio reactivity ("destructive but resisted").
    system_->SetForce(shapeConformForceIndex_,
        gpu_force::ShapeConform(force.shapeAttraction, force.shapeCurl, morphStrength, attraction.recruitFraction,
                                 attraction.volumeDepth, force.flowNoiseScale));
    system_->SetForce(gravityForceIndex_, gpu_force::GravityWell(field.Center(), force.gravityStrength, force.gravitySoftening));

    // Turbulence is the one force audio is allowed to add to: the base
    // slider stays the resting/ambient value, audio adds on top for this
    // frame only (never written back into force.turbulenceStrength
    // itself) -- same "base + audio scale" pattern FogLightingSettings'
    // hue/intensity fields already use. Excitement is a slow envelope
    // (eased toward Energy() at a tunable rate) so the ambient turbulence
    // floor breathes with the track instead of jittering every FFT
    // update; Treble is read raw for a faster, fizzier top-end response.
    float effectiveTurbulence = force.turbulenceStrength;
    if (audio.reactive) {
        float smoothing = std::max(audio.excitementSmoothing, 0.01f);
        excitement_ += (audioAnalyzer.Energy() - excitement_) * std::min(1.0f, dt / smoothing);
        effectiveTurbulence += excitement_ * audio.turbulenceEnergyScale + audioAnalyzer.Treble() * audio.turbulenceTrebleScale;
    }
    system_->SetForce(turbulenceForceIndex_, gpu_force::Turbulence(effectiveTurbulence, force.turbulenceScale));
    system_->SetForce(dragForceIndex_, gpu_force::Drag(force.dragCoefficient));

    // Cheap CPU-side (two field writes -- see GpuParticleSystem::SetShading),
    // so re-calling every frame is fine and is what makes Lighting's
    // Overhead Light Position / Shade Ambient Floor live-tunable.
    system_->SetShading(shadeLightDir, shadeAmbientFloor);

    // Emission: every particle is born already on (or just off) the
    // *current* shape's surface via GpuEmitMode::ShapeSurface -- a single
    // Newton/gradient step against the bound ShapeField (see
    // particle_emit.comp), not a slow drift-in from a diffuse spawn
    // volume. This is the standard professional-VFX pattern for
    // "structure that reacts instantly": bias birth position by the SDF
    // instead of relying purely on a force to drag particles there over
    // several seconds.
    //
    // Life is differentiated (ep.life vs ep.shedLife, both gated by
    // ep.recruitFraction -- see particle_emit.comp and
    // GpuEmitParams::recruitFraction): the recruited fraction
    // (FogAttractionSettings::recruitFraction) gets a long life and is
    // what the ShapeConform force actually holds onto the shape, so it
    // persists across a shape change and *flows* from the old silhouette
    // to the new one -- this is what makes the whole thing read as one
    // volume morphing, not a fresh population replacing the old one. Only
    // the fraction that misses recruitment gets a short life, reading as
    // occasional wisps peeling off and dissipating.
    spawnAccumulator_ += dt * emission.spawnRate;
    int spawnCount = static_cast<int>(spawnAccumulator_);
    if (spawnCount > 0) {
        spawnAccumulator_ -= static_cast<float>(spawnCount);

        GpuEmitParams ep;
        ep.mode = GpuEmitMode::ShapeSurface;
        ep.position = field.Center();
        ep.positionJitter = { attraction.candidateHalfExtent, attraction.candidateHalfExtent, attraction.candidateHalfExtent };
        ep.shellThickness = attraction.shellThickness;
        ep.velocity = emission.velocity;
        ep.velocityJitter = emission.velocityJitter;

        ep.colorA = emission.colorA;
        ep.colorB = emission.colorB;

        ep.size = emission.size;
        ep.sizeJitter = emission.sizeJitter;

        ep.life = emission.life;
        ep.lifeJitter = emission.lifeJitter;
        ep.recruitFraction = attraction.recruitFraction;
        ep.shedLife = emission.shedLife;
        ep.shedLifeJitter = emission.shedLifeJitter;

        system_->Emit(ep, spawnCount);
    }

    system_->Update(dt, time);
}

void ShapeFogEmitter::Draw(const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
                            const LightSample* lights, int lightCount, float time, int spriteStyle) const {
    if (!system_) return;
    // fadeMode 2 (particle_render.vert's two-sided fade, in over the first
    // ~25% of life and out over the final ~17%) instead of 1 (instant-
    // full-opacity-at-spawn): with mode 1 every newly spawned particle
    // snapped to full brightness the instant it existed, which read as a
    // visible "pop in" no matter how the lifecycle/spawn-rate was tuned.
    system_->Draw(viewProj, cameraRight, cameraUp, /*fadeMode=*/2, /*sizeScale=*/1.0f,
                  lights, lightCount, time, spriteStyle);
}

void ShapeFogEmitter::VisitSettings(ui::IParamVisitor& v) {
    v.BeginGroup("Emission");
    emission.Visit(v);
    v.EndGroup();

    v.BeginGroup("Forces");
    force.Visit(v);
    v.EndGroup();

    v.BeginGroup("Attraction");
    attraction.Visit(v);
    v.EndGroup();

    v.BeginGroup("Audio");
    audio.Visit(v);
    v.EndGroup();
}
