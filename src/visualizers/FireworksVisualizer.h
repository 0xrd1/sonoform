#pragma once
#include <vector>
#include <memory>
#include "Visualizer.h"
#include "GpuParticleSystem.h"
#include "AudioAnalyzer.h"

// Beats launch a shell that arcs upward and explodes into a radial burst
// of GPU-simulated particles under gravity + drag, classic fireworks-style.
// Shell ballistics themselves stay on the CPU (there are only ever a
// handful alive at once); the burst itself -- up to a couple hundred
// particles with independently random directions -- is one GPU dispatch
// via GpuEmitMode::Sphere instead of a CPU loop of single-particle emits.
class FireworksVisualizer : public Visualizer {
public:
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) override;
    void Update(const FrameContext& frame) override;
    void Draw(const RenderContext& ctx) const override;
    const char* Name() const override { return "Fireworks"; }
    int ParticleCount() const override { return particles_ ? particles_->AliveCountApprox() : 0; }

private:
    struct Shell {
        Vector3 position;
        Vector3 velocity;
        float fuse;  // seconds until explosion
        float hue;
    };

    void Explode(const Shell& shell, float intensity);

    std::unique_ptr<GpuParticleSystem> particles_;
    std::vector<Shell> shells_;
};
