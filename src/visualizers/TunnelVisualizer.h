#pragma once
#include <memory>
#include "Visualizer.h"
#include "GpuParticleSystem.h"
#include "AudioAnalyzer.h"

// Particles spawn in a ring at far Z and fly toward the camera; ring
// radius per-angle is modulated by the spectrum bars, so the tunnel
// wobbles and pulses with the music. Colors cycle continuously and flash
// brighter on beats.
class TunnelVisualizer : public Visualizer {
public:
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) override;
    void Update(const FrameContext& frame) override;
    void Draw(const RenderContext& ctx) const override;
    const char* Name() const override { return "Audio Tunnel"; }
    int ParticleCount() const override { return particles_ ? particles_->AliveCountApprox() : 0; }

private:
    std::unique_ptr<GpuParticleSystem> particles_;
    float spawnAccumulator_ = 0.0f;
    float beatFlash_ = 0.0f;
    float baseRadius_ = 3.0f;
    float farZ_ = -60.0f;
};
