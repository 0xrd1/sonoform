#include "TunnelVisualizer.h"
#include "ForceFactory.h"
#include "raymath.h"
#include <cmath>
#include <random>
#include <algorithm>

namespace {
std::mt19937 gRng{ 7331u };
float RandUnit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(gRng); }
}

void TunnelVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    particles_ = std::make_unique<GpuParticleSystem>(shaders, renderer, 16000);
    particles_->AddForce(gpu_force::Turbulence(0.5f, 0.2f));
}

void TunnelVisualizer::Update(const FrameContext& frame) {
    float speed = 10.0f + frame.audio.Energy() * 26.0f * frame.intensity;
    float spawnRate = 500.0f * frame.intensity;
    spawnAccumulator_ += frame.dt * spawnRate;

    const auto& bars = frame.audio.Bars();
    const int n = AudioAnalyzer::kNumBars;

    while (spawnAccumulator_ >= 1.0f) {
        float angle = RandUnit() * 2.0f * 3.14159265f;
        int barIdx = static_cast<int>((angle / (2.0f * 3.14159265f)) * n) % n;
        float bar = bars[static_cast<size_t>(barIdx)];
        float radius = baseRadius_ + bar * 5.0f;

        GpuEmitParams ep;
        ep.position = { std::cos(angle) * radius, std::sin(angle) * radius, farZ_ };
        ep.velocity = { 0, 0, speed };
        ep.velocityJitter = { 0.2f, 0.2f, 0.5f };

        float hue = std::fmod(frame.time * 40.0f + angle * (180.0f / 3.14159265f), 360.0f);
        float value = 0.7f + beatFlash_ * 0.3f;
        ep.colorA = ColorFromHSV(hue, 0.75f, std::min(1.0f, value));
        ep.colorB = ColorFromHSV(std::fmod(hue + 30.0f, 360.0f), 0.6f, std::min(1.0f, value));
        ep.size = 0.12f + bar * 0.06f;
        ep.life = std::fabs(farZ_) / speed;
        particles_->Emit(ep, 1);

        spawnAccumulator_ -= 1.0f;
    }

    if (frame.audio.BeatTriggered()) {
        beatFlash_ = 1.0f;
    }
    beatFlash_ = std::max(0.0f, beatFlash_ - frame.dt * 2.5f);

    particles_->Update(frame.dt, frame.time);
}

void TunnelVisualizer::Draw(const RenderContext& ctx) const {
    BeginBlendMode(BLEND_ADDITIVE);
    if (particles_) particles_->Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, /*fadeMode=*/2, /*sizeScale=*/1.0f);
    EndBlendMode();
}
