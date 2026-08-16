#include "SpectrumRingVisualizer.h"
#include "ForceFactory.h"
#include "raymath.h"
#include <cmath>
#include <algorithm>

void SpectrumRingVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    sparks_ = std::make_unique<GpuParticleSystem>(shaders, renderer, 6000);
    sparks_->AddForce(gpu_force::Directional({ 0, -1, 0 }, 5.0f));
    sparks_->AddForce(gpu_force::Drag(0.4f));
    barHeights_.fill(0.0f);
}

void SpectrumRingVisualizer::Update(const FrameContext& frame) {
    const int n = AudioAnalyzer::kNumBars;
    const auto& bars = frame.audio.Bars();

    for (int i = 0; i < n; i++) {
        float target = bars[static_cast<size_t>(i)] * 3.0f * frame.intensity;
        barHeights_[static_cast<size_t>(i)] += (target - barHeights_[static_cast<size_t>(i)]) * std::min(1.0f, frame.dt * 8.0f);

        float h = barHeights_[static_cast<size_t>(i)];
        if (h > 0.35f) {
            float angle = (static_cast<float>(i) / n) * 2.0f * 3.14159265f;
            Vector3 pos{ std::cos(angle) * ringRadius_, h, std::sin(angle) * ringRadius_ };
            Vector3 outward{ std::cos(angle), 0.6f, std::sin(angle) };

            GpuEmitParams ep;
            ep.position = pos;
            ep.positionJitter = { 0.1f, 0.05f, 0.1f };
            ep.velocity = Vector3Scale(outward, 1.5f + h);
            ep.velocityJitter = { 0.4f, 0.4f, 0.4f };
            float hue = (static_cast<float>(i) / n) * 300.0f;
            ep.colorA = ColorFromHSV(hue, 0.85f, 1.0f);
            ep.colorB = ColorFromHSV(hue + 20.0f, 0.6f, 1.0f);
            ep.size = 0.12f; ep.sizeJitter = 0.05f;
            ep.life = 1.1f; ep.lifeJitter = 0.4f;
            sparks_->Emit(ep, static_cast<int>(h * 3.0f * frame.intensity));
        }
    }

    if (frame.audio.BeatTriggered()) {
        pulses_.push_back(Pulse{ ringRadius_ * 0.4f, 1.0f });
        sparks_->ApplyRadialImpulse({ 0, 0.5f, 0 }, 6.0f * frame.intensity * (0.5f + frame.audio.BeatIntensity()), ringRadius_ * 2.5f);
    }

    for (auto it = pulses_.begin(); it != pulses_.end();) {
        it->radius += frame.dt * 9.0f;
        it->alpha -= frame.dt * 1.3f;
        if (it->alpha <= 0.0f) it = pulses_.erase(it);
        else ++it;
    }

    sparks_->Update(frame.dt, frame.time);
}

void SpectrumRingVisualizer::Draw(const RenderContext& ctx) const {
    BeginBlendMode(BLEND_ADDITIVE); // self-luminous bars/sparks; see App::Draw's comment on why this is per-visualizer now
    const int n = AudioAnalyzer::kNumBars;
    for (int i = 0; i < n; i++) {
        float angle = (static_cast<float>(i) / n) * 2.0f * 3.14159265f;
        float h = std::max(0.05f, barHeights_[static_cast<size_t>(i)]);
        Vector3 base{ std::cos(angle) * ringRadius_, 0.0f, std::sin(angle) * ringRadius_ };
        Vector3 top{ base.x, h, base.z };
        float hue = (static_cast<float>(i) / n) * 300.0f;
        Color col = ColorFromHSV(hue, 0.85f, 1.0f);
        DrawCylinderEx(base, top, 0.12f, 0.12f, 8, col);
    }

    for (const auto& pulse : pulses_) {
        DrawCircle3D({ 0, 0.05f, 0 }, pulse.radius, { 1, 0, 0 }, 90.0f, Fade(WHITE, pulse.alpha));
    }

    if (sparks_) sparks_->Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, /*fadeMode=*/0, /*sizeScale=*/1.0f);
    EndBlendMode();
}
