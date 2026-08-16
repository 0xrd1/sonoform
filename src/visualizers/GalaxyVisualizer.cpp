#include "GalaxyVisualizer.h"
#include "ForceFactory.h"
#include "raymath.h"
#include <cmath>
#include <algorithm>

void GalaxyVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    stars_ = std::make_unique<GpuParticleSystem>(shaders, renderer, 18000);

    attractorForceIndex_ = stars_->AddForce(
        gpu_force::GravityWell(attractorCenter_, attractorStrength_, attractorSoftening_));
    turbulenceForceIndex_ = stars_->AddForce(
        gpu_force::Turbulence(turbulenceStrength_, turbulenceScale_));
    stars_->AddForce(gpu_force::Drag(0.05f));
}

void GalaxyVisualizer::Update(const FrameContext& frame) {
    attractorStrength_ = 6.0f + frame.audio.Bass() * 60.0f * frame.intensity;
    turbulenceStrength_ = 0.4f + frame.audio.Treble() * 8.0f * frame.intensity;

    stars_->SetForce(attractorForceIndex_,
        gpu_force::GravityWell(attractorCenter_, attractorStrength_, attractorSoftening_));
    stars_->SetForce(turbulenceForceIndex_,
        gpu_force::Turbulence(turbulenceStrength_, turbulenceScale_));

    float spawnRate = 60.0f + frame.audio.Energy() * 400.0f * frame.intensity;
    spawnAccumulator_ += frame.dt * spawnRate;

    int spawnCount = static_cast<int>(spawnAccumulator_);
    if (spawnCount > 0) {
        spawnAccumulator_ -= static_cast<float>(spawnCount);

        GpuEmitParams ep;
        ep.mode = GpuEmitMode::Orbit;
        ep.position = attractorCenter_;
        ep.positionJitter = { 7.0f, 13.0f, 0.75f }; // radiusMin, radiusMax, heightJitter (matches old radius 7..13, height ±0.75)
        ep.attractorStrength = attractorStrength_;
        ep.orbitSpeedMultiplier = 1.8f;
        ep.orbitVelocityJitter = 0.15f;

        // Inner/outer hue endpoints; the emit shader lerps between them by
        // each particle's actual spawn radius (see EMIT_MODE_ORBIT's
        // colorT in particle_emit.comp), reproducing the CPU original's
        // radius-based hue gradient (hue = 200 + radius*6) plus its slow
        // sin(time*0.2)*20 drift, now applied to both endpoints at once
        // rather than per particle -- a close approximation, not a
        // per-particle-exact reproduction of the old ±40deg local jitter.
        float drift = std::sin(frame.time * 0.2f) * 20.0f;
        float innerHue = std::fmod(200.0f + ep.positionJitter.x * 6.0f + drift + 360.0f, 360.0f);
        float outerHue = std::fmod(200.0f + ep.positionJitter.y * 6.0f + drift + 360.0f, 360.0f);
        ep.colorA = ColorFromHSV(innerHue, 0.7f, 1.0f);
        ep.colorB = ColorFromHSV(outerHue, 0.5f, 1.0f);
        ep.size = 0.08f; ep.sizeJitter = 0.03f;
        ep.life = 6.0f; ep.lifeJitter = 2.0f;

        stars_->Emit(ep, spawnCount);
    }

    if (frame.audio.BeatTriggered()) {
        stars_->ApplyRadialImpulse(attractorCenter_, 10.0f * frame.intensity * (0.5f + frame.audio.BeatIntensity()), 40.0f);
    }

    stars_->Update(frame.dt, frame.time);
}

void GalaxyVisualizer::Draw(const RenderContext& ctx) const {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawBillboard(ctx.camera, ctx.accentTexture, attractorCenter_, 1.2f, Fade(WHITE, 0.9f));
    DrawBillboard(ctx.camera, ctx.accentTexture, attractorCenter_, 0.5f, WHITE);

    if (stars_) stars_->Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, /*fadeMode=*/1, /*sizeScale=*/1.0f);
    EndBlendMode();
}
