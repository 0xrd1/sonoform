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

    // Distinct musical features drive distinct forces -- see
    // FogAudioSettings' class comment for why each target is safe to
    // drive hard without ever weakening the shape's hold: Gravity has
    // always been "loose containment, well below Shape Attraction";
    // Shape Curl is tangential-only (shape_conform.glsl projects it
    // perpendicular to the gradient, so it can slide particles across the
    // surface but never pull them off it). Bass/Mid are smoothed (eased
    // toward the raw band value at audio.motionSmoothing's rate) so the
    // forces they drive breathe with the track instead of jittering every
    // FFT update; Treble stays instantaneous, the deliberately fast
    // "sparkle" signal. None of this ever touches shapeAttraction/
    // morphStrength/recruitFraction -- the panel/driver values for those
    // are passed through completely un-modulated below.
    float effectiveGravity = force.gravityStrength;
    float effectiveCurl = force.shapeCurl;
    float effectiveTurbulence = force.turbulenceStrength;
    if (audio.reactive) {
        float smoothing = std::max(audio.motionSmoothing, 0.01f);
        float lag = std::min(1.0f, dt / smoothing);
        bassSmoothed_ += (audioAnalyzer.Bass() - bassSmoothed_) * lag;
        midSmoothed_ += (audioAnalyzer.Mid() - midSmoothed_) * lag;
        excitement_ += (audioAnalyzer.Energy() - excitement_) * lag;

        effectiveGravity += bassSmoothed_ * audio.bassGravityScale;
        effectiveCurl += midSmoothed_ * audio.midCurlScale;
        effectiveTurbulence += excitement_ * audio.excitementTurbulenceScale + audioAnalyzer.Treble() * audio.trebleTurbulenceScale;
    }

    system_->SetForce(shapeConformForceIndex_,
        gpu_force::ShapeConform(force.shapeAttraction, effectiveCurl, morphStrength, attraction.recruitFraction,
                                 attraction.volumeDepth, force.flowNoiseScale));
    system_->SetForce(gravityForceIndex_, gpu_force::GravityWell(field.Center(), effectiveGravity, force.gravitySoftening));
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
                            const LightSample* lights, int lightCount, float time, int spriteStyle,
                            const PaletteParams& palette) const {
    if (!system_) return;
    // fadeMode 2 (particle_render.vert's two-sided fade, in over the first
    // ~25% of life and out over the final ~17%) instead of 1 (instant-
    // full-opacity-at-spawn): with mode 1 every newly spawned particle
    // snapped to full brightness the instant it existed, which read as a
    // visible "pop in" no matter how the lifecycle/spawn-rate was tuned.
    system_->Draw(viewProj, cameraRight, cameraUp, /*fadeMode=*/2, /*sizeScale=*/1.0f,
                  lights, lightCount, time, spriteStyle, palette);
}

void ShapeFogEmitter::EmitImpactBurst(Vector3 center) {
    if (!system_ || audio.kickBurstCount <= 0) return;

    GpuEmitParams ep;
    ep.mode = GpuEmitMode::Sphere; // uniform random outward direction -- see GpuEmitMode's comment
    ep.position = center;
    ep.positionJitter = { attraction.candidateHalfExtent, attraction.candidateHalfExtent, attraction.candidateHalfExtent };
    ep.speedMin = audio.kickBurstSpeed;
    ep.speedMax = audio.kickBurstSpeed + audio.kickBurstSpeedJitter;

    ep.colorA = emission.colorA;
    ep.colorB = emission.colorB;
    ep.size = emission.size;
    ep.sizeJitter = emission.sizeJitter;

    // recruitFraction 0 -- see the header's comment: guaranteed to take
    // the shed-life branch (particle_emit.comp), so every burst particle
    // flies out, fades, and despawns, never gets pulled back by Shape
    // Attraction the way a KickImpulse-nudged recruited particle would.
    ep.recruitFraction = 0.0f;
    ep.shedLife = audio.kickBurstLife;
    ep.shedLifeJitter = audio.kickBurstLifeJitter;
    ep.life = audio.kickBurstLife; // unused at recruitFraction 0, set to a sane value regardless
    ep.lifeJitter = audio.kickBurstLifeJitter;

    system_->Emit(ep, audio.kickBurstCount);
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
