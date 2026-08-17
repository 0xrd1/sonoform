#pragma once
#include <vector>
#include "raylib.h"
#include "LightSample.h"

namespace ui { struct LightningSettings; }
class ShapeField;

// Fractal lightning bolts: strong beats/treble transients spawn bolts
// built via recursive midpoint displacement (each segment split in half
// with a random perpendicular offset, halved each level, plus a few
// shorter side-branches) for a natural jagged silhouette. Both endpoints
// of every segment (trunk and branches) are rejection-sampled from
// *inside* the shape's real baked SDF (see SampleInsidePoint in the
// .cpp) rather than extended a fixed length in a random direction from a
// single origin point -- so bolts always arc within the current shape's
// actual silhouette and automatically scale to whatever shape/size is
// currently baked, instead of routinely shooting off into empty space
// (an earlier direction+length version's bug). Each bolt fades on a
// sharp-attack, fast-decay flicker envelope.
//
// Rendered directly as double-pass cylinders (bright core + wide dim
// halo) rather than through the GPU particle pipeline -- there are only
// ever a handful of bolts alive at once, so CPU-side geometry is simpler
// and plenty fast. Also exposed as point-light samples (GatherLights) so
// NeonFogVisualizer can feed them into GpuParticleSystem::Draw and light
// nearby fog particles from within.
class LightningSystem {
public:
    // `field`: the shape bolts are sampled from/contained within -- see
    // the class comment; requires field.SampleWorld() to be current (the
    // caller refreshes ShapeField's sample cache on every rebake, not
    // this class's concern). `trigger`: fire a new bolt this frame.
    // `triggerStrength` in [0,1] scales branch count and brightness.
    // `settings` supplies every other tunable (was file-local literals in
    // SpawnBolt) -- see ui::LightningSettings in src/ui/EngineSettings.h.
    void Update(float dt, const ShapeField& field, bool trigger, float triggerStrength, const ui::LightningSettings& settings);

    void Draw(const ui::LightningSettings& settings) const;

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

    void SpawnBolt(const ShapeField& field, float strength, const ui::LightningSettings& settings);

    std::vector<Bolt> bolts_;
    unsigned int rngState_ = 0x9E3779B9u;
};
