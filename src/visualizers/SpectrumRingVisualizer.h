#pragma once
#include <array>
#include <vector>
#include <memory>
#include "Visualizer.h"
#include "GpuParticleSystem.h"
#include "AudioAnalyzer.h"

// A ring of frequency bars (classic circular spectrum analyzer), each
// shooting glowing GPU-simulated sparks that fall under gravity + drag.
// Beats trigger an expanding pulse ring and a radial spark burst.
class SpectrumRingVisualizer : public Visualizer {
public:
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) override;
    void Update(const FrameContext& frame) override;
    void Draw(const RenderContext& ctx) const override;
    const char* Name() const override { return "Spectrum Ring"; }
    int ParticleCount() const override { return sparks_ ? sparks_->AliveCountApprox() : 0; }

private:
    struct Pulse { float radius; float alpha; };

    std::unique_ptr<GpuParticleSystem> sparks_;
    std::array<float, AudioAnalyzer::kNumBars> barHeights_{};
    std::vector<Pulse> pulses_;
    float ringRadius_ = 6.0f;
};
