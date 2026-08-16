#pragma once
#include <memory>
#include "Visualizer.h"
#include "GpuParticleSystem.h"
#include "AudioAnalyzer.h"

// A central gravity well with continuously spawned stars given tangential
// velocity to fall into orbit, forming a spiral-galaxy look. Bass energy
// strengthens the well and spawn rate; treble drives turbulence; beats
// kick particles outward for a pulsing effect.
class GalaxyVisualizer : public Visualizer {
public:
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer) override;
    void Update(const FrameContext& frame) override;
    void Draw(const RenderContext& ctx) const override;
    const char* Name() const override { return "Particle Galaxy"; }
    int ParticleCount() const override { return stars_ ? stars_->AliveCountApprox() : 0; }

private:
    std::unique_ptr<GpuParticleSystem> stars_;

    // Cached CPU-side copies of the mutable forces (source of truth,
    // pushed to the GPU via SetForce whenever changed) -- the GPU
    // equivalent of the old CPU pattern of holding a shared_ptr<IForce>
    // and rewriting its fields every frame.
    Vector3 attractorCenter_{ 0, 0, 0 };
    float attractorStrength_ = 8.0f;
    float attractorSoftening_ = 1.0f;
    int attractorForceIndex_ = -1;

    float turbulenceStrength_ = 0.6f;
    float turbulenceScale_ = 0.12f;
    int turbulenceForceIndex_ = -1;

    float spawnAccumulator_ = 0.0f;
};
