#pragma once
#include <vector>
#include "raylib.h"
#include "LightSample.h"

// Fractal lightning bolts: strong beats/treble transients spawn bolts
// built via recursive midpoint displacement (each segment split in half
// with a random perpendicular offset, halved each level, plus a few
// shorter side-branches) for a natural jagged silhouette, arcing from
// near the fog's structural core outward. Each bolt fades on a sharp-
// attack, fast-decay flicker envelope.
//
// Rendered directly as double-pass cylinders (bright core + wide dim
// halo) rather than through the GPU particle pipeline -- there are only
// ever a handful of bolts alive at once, so CPU-side geometry is simpler
// and plenty fast. Also exposed as point-light samples (GatherLights) so
// NeonFogVisualizer can feed them into GpuParticleSystem::Draw and light
// nearby fog particles from within.
class LightningSystem {
public:
    // `origin`: roughly the fog's structural center, where bolts
    // originate. `trigger`: fire a new bolt this frame. `triggerStrength`
    // in [0,1] scales bolt length, branch count, and brightness.
    void Update(float dt, Vector3 origin, bool trigger, float triggerStrength);

    void Draw() const;

    // Fills `outLights` with up to `maxLights` samples from active bolts,
    // returns how many were written.
    int GatherLights(LightSample* outLights, int maxLights) const;

private:
    struct Bolt {
        std::vector<Vector3> points;
        std::vector<std::vector<Vector3>> branches;
        float life = 0.0f;
        float maxLife = 0.15f;
        Color color = WHITE;
        float brightness = 1.0f;
    };

    void SpawnBolt(Vector3 origin, float strength);

    std::vector<Bolt> bolts_;
    unsigned int rngState_ = 0x9E3779B9u;
};
