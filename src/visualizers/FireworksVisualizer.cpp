#include "FireworksVisualizer.h"
#include "ForceFactory.h"
#include "raymath.h"
#include <cmath>
#include <random>
#include <algorithm>

namespace {
std::mt19937 gRng{ 99u };
float RandRange(float a, float b) { return a + std::uniform_real_distribution<float>(0.0f, 1.0f)(gRng) * (b - a); }
}

void FireworksVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    particles_ = std::make_unique<GpuParticleSystem>(shaders, renderer, 20000);
    particles_->AddForce(gpu_force::Directional({ 0, -1, 0 }, 5.5f));
    particles_->AddForce(gpu_force::Drag(0.5f));
}

void FireworksVisualizer::Explode(const Shell& shell, float intensity) {
    int count = static_cast<int>(RandRange(140.0f, 260.0f) * std::min(1.5f, 0.6f + intensity * 0.7f));

    // A single hue jitter for the whole burst, spread further by the
    // shader's independent per-particle color lerp -- approximates the
    // CPU original's compound per-particle hueJitter + lerp-t randomness
    // (see FireworksVisualizer history) without needing per-particle CPU
    // draws now that the burst is one GPU dispatch.
    float hueJitter = RandRange(-15.0f, 15.0f);

    GpuEmitParams ep;
    ep.mode = GpuEmitMode::Sphere;
    ep.position = shell.position;
    ep.speedMin = 3.0f * (0.7f + intensity * 0.5f);
    ep.speedMax = 8.0f * (0.7f + intensity * 0.5f);
    ep.colorA = ColorFromHSV(std::fmod(shell.hue + hueJitter + 360.0f, 360.0f), 0.9f, 1.0f);
    ep.colorB = ColorFromHSV(std::fmod(shell.hue + hueJitter + 40.0f + 360.0f, 360.0f), 0.6f, 1.0f);
    ep.size = 0.1f; ep.sizeJitter = 0.04f;
    ep.life = 1.5f; ep.lifeJitter = 0.5f; // uniform over [1.0, 2.0], matching the CPU original

    particles_->Emit(ep, count);
}

void FireworksVisualizer::Update(const FrameContext& frame) {
    if (frame.audio.BeatTriggered()) {
        int shellCount = 1 + (frame.audio.BeatIntensity() > 0.6f ? 1 : 0);
        for (int i = 0; i < shellCount; i++) {
            Shell shell;
            shell.position = { RandRange(-4.0f, 4.0f), 0.0f, RandRange(-4.0f, 4.0f) };
            float apexHeight = RandRange(5.0f, 9.0f) * (0.7f + frame.intensity * 0.4f);
            float apexTime = RandRange(0.7f, 1.1f);
            float gravity = 5.5f;
            float vy0 = gravity * apexTime + apexHeight / apexTime;
            shell.velocity = { RandRange(-0.6f, 0.6f), vy0, RandRange(-0.6f, 0.6f) };
            shell.fuse = apexTime;
            shell.hue = RandRange(0.0f, 360.0f);
            shells_.push_back(shell);
        }
    }

    for (auto it = shells_.begin(); it != shells_.end();) {
        it->velocity.y -= 5.5f * frame.dt;
        it->position = Vector3Add(it->position, Vector3Scale(it->velocity, frame.dt));
        it->fuse -= frame.dt;

        GpuEmitParams trail;
        trail.position = it->position;
        trail.velocity = Vector3Scale(it->velocity, -0.1f);
        trail.colorA = ColorFromHSV(it->hue, 0.5f, 1.0f);
        trail.colorB = ColorFromHSV(it->hue, 0.2f, 1.0f);
        trail.size = 0.07f;
        trail.life = 0.3f;
        particles_->Emit(trail, 1);

        if (it->fuse <= 0.0f) {
            Explode(*it, frame.intensity);
            it = shells_.erase(it);
        } else {
            ++it;
        }
    }

    particles_->Update(frame.dt, frame.time);
}

void FireworksVisualizer::Draw(const RenderContext& ctx) const {
    BeginBlendMode(BLEND_ADDITIVE);
    for (const auto& shell : shells_) {
        DrawBillboard(ctx.camera, ctx.accentTexture, shell.position, 0.15f, ColorFromHSV(shell.hue, 0.6f, 1.0f));
    }

    if (particles_) particles_->Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, /*fadeMode=*/0, /*sizeScale=*/1.0f);
    EndBlendMode();
}
