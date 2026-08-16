#pragma once
#include <vector>
#include "raylib.h"
#include "GpuBuffer.h"
#include "ParticleTypes.h"
#include "LightSample.h"

class ShaderLibrary;
class ParticleRenderer;
class ShapeField;

// How a GpuParticleSystem::Emit() call randomizes each new particle's
// velocity. The old CPU EmitParams only ever needed a jittered base
// velocity; FireworksVisualizer's explosion burst needed a uniformly
// random direction instead, which was expressed as a loop of many
// single-particle CPU emits. On the GPU, "spawn N particles each with an
// independent random direction" is naturally one dispatch of N threads,
// so that pattern becomes its own explicit mode instead of a call-site loop.
enum class GpuEmitMode {
    Box = 0,     // velocity = base + independent per-axis jitter
    Sphere = 1,  // velocity = uniform-random direction * random speed in [speedMin, speedMax]
    Orbit = 2,   // position on an annulus around `position`; velocity = tangential orbital speed + jitter
                 // (replaces GalaxyVisualizer's old per-particle CPU orbit-velocity loop)
    ShapeSurface = 3, // position = a candidate point in the jitter box, projected onto the
                      // system's bound ShapeField via one gradient (Newton) step, +/- shellThickness
                      // along the surface normal. Requires SetShapeField() to have been called; see
                      // GpuEmitParams::shellThickness. Puts freshly-spawned particles on/near the
                      // *current* shape immediately instead of waiting for a ShapeConform force to
                      // drag them there over several seconds -- the professional-VFX pattern of
                      // biasing birth position by an SDF (cf. Houdini POPs-in-context / Niagara's
                      // SDF-driven "Shape Location").
};

// Mirrors the old CPU EmitParams (src/particles/ParticleSystem.h, now
// deleted) plus the explicit `mode` above.
struct GpuEmitParams {
    Vector3 position{ 0, 0, 0 };        // base position / Orbit mode's center
    Vector3 positionJitter{ 0, 0, 0 };  // Box/Sphere/ShapeSurface: box half-extents (candidate region
                                         // for ShapeSurface, projected onto the field afterward).
                                         // Orbit: x=radiusMin,y=radiusMax,z=heightJitter
    GpuEmitMode mode = GpuEmitMode::Box;
    float shellThickness = 0.0f;        // ShapeSurface mode only: +/- random offset along the
                                         // surface normal after projection, so the birth shell has
                                         // a little softness instead of being a razor-thin surface.

    Vector3 velocity{ 0, 0, 0 };        // Box mode
    Vector3 velocityJitter{ 0, 0, 0 };  // Box mode
    float speedMin = 0.0f;              // Sphere mode
    float speedMax = 0.0f;              // Sphere mode

    float attractorStrength = 0.0f;      // Orbit mode: current GravityWell strength, for orbital speed = sqrt(strength/radius)
    float orbitSpeedMultiplier = 1.0f;   // Orbit mode
    float orbitVelocityJitter = 0.0f;    // Orbit mode: isotropic jitter added on top of tangential velocity

    Color colorA = WHITE;
    Color colorB = WHITE;
    float size = 0.15f, sizeJitter = 0.0f;
    float life = 1.0f, lifeJitter = 0.0f;
};

// One GPU-simulated, GPU-rendered particle pool: owns its SSBOs and force
// list and drives the compute -> render pipeline (particle_sim.comp ->
// particle_emit.comp -> ParticleRenderer's instanced vertex-pulled draw).
// Ported in spirit from the old CPU particles/ParticleSystem, but no
// particle state ever touches the CPU.
class GpuParticleSystem {
public:
    GpuParticleSystem(ShaderLibrary& shaders, ParticleRenderer& renderer, int capacity);
    ~GpuParticleSystem();

    GpuParticleSystem(const GpuParticleSystem&) = delete;
    GpuParticleSystem& operator=(const GpuParticleSystem&) = delete;

    // Force list management. Returns an index that SetForce can later use
    // to mutate that force's parameters in place -- the GPU equivalent of
    // the old CPU pattern where a visualizer kept a shared_ptr<IForce> and
    // rewrote its fields every frame (see GalaxyVisualizer).
    int AddForce(const GpuForceDesc& force);
    void SetForce(int index, const GpuForceDesc& force);
    void ClearForces();
    const GpuForceDesc& Force(int index) const { return forces_[static_cast<size_t>(index)]; }

    // Associates a ShapeField this system's ShapeConform forces (if any)
    // should sample. Bound and its grid-transform uniforms set on every
    // Update() call when non-null. Pass nullptr to disassociate; the
    // field pointer is not owned.
    void SetShapeField(ShapeField* field) { shapeField_ = field; }

    // Fake volumetric self-shadow: whenever a ShapeField is bound, Update()
    // samples its gradient each frame as a stand-in surface normal and
    // writes a light-facing factor into each particle's Particle.params.w
    // (see particle_sim.comp), which particle_render.vert multiplies into
    // the base color. `lightDir` should point FROM the structure TOWARD
    // the key light; normalized on store. `ambientFloor` keeps the
    // shadowed side dim rather than pure black. No effect on systems that
    // never call SetShapeField. Call once after setup, or whenever the
    // light changes -- see NeonFogVisualizer::Init.
    void SetShading(Vector3 lightDir, float ambientFloor);

    // Spawns `count` particles per the given parameters. Silently drops
    // any that don't fit (pool at capacity), matching the old CPU
    // ParticleSystem::Emit's behavior.
    void Emit(const GpuEmitParams& params, int count);

    // One-shot outward velocity kick to every alive particle within
    // maxRadius of center. GPU equivalent of the old CPU
    // ParticleSystem::ApplyRadialImpulse; takes effect on the next
    // Update() call and then auto-clears, so call this before Update()
    // in the same frame (matching the old call order).
    void ApplyRadialImpulse(Vector3 center, float strength, float maxRadius);

    void Update(float dt, float time);

    // fadeMode: 0 = linear life fade, 1 = eased (min(1, ratio*1.5)),
    // 2 = two-sided edge fade -- see particle_render.vert's FadeCurve.
    // lights/lightCount let e.g. lightning bolts illuminate this system's
    // particles from within; see gfx/LightSample.h.
    void Draw(const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
              int fadeMode = 0, float sizeScale = 1.0f,
              const LightSample* lights = nullptr, int lightCount = 0,
              float time = 0.0f) const;

    int Capacity() const { return capacity_; }

    // Reads back the free-list counter to report roughly how many
    // particles are alive (capacity_ - freeCount). This is a genuine
    // GPU->CPU readback (rlReadShaderBuffer), unlike Draw() -- which
    // never reads anything back, it draws `capacity_` instances every
    // frame and lets the vertex shader collapse dead ones to degenerate
    // triangles. Call this sparingly (e.g. once every N frames for a HUD
    // counter), not every frame, to avoid pipeline stalls.
    int AliveCountApprox() const;

    // Debug utility: dumps the first `count` particle slots' raw field
    // values via TraceLog(LOG_INFO, ...). Not wired to any input by
    // default -- call it from a debugger or a temporary call site when
    // diagnosing simulation issues.
    void DebugDumpFirst(int count) const;

private:
    ShaderLibrary& shaders_;
    ParticleRenderer& renderer_;
    int capacity_;

    GpuBuffer particleBuffer_;
    GpuBuffer freeListBuffer_;
    GpuBuffer forceBuffer_;
    std::vector<GpuForceDesc> forces_;
    bool forcesDirty_ = true;

    unsigned int simProgram_ = 0;
    unsigned int emitProgram_ = 0;

    unsigned int emitSeedCounter_ = 1;

    ShapeField* shapeField_ = nullptr; // not owned; see SetShapeField

    Vector3 shadingLightDir_{ 0, 1, 0 };
    float shadingAmbientFloor_ = 1.0f; // 1.0 = no-op (no darkening) until SetShading is called

    Vector3 pendingImpulseCenter_{ 0, 0, 0 };
    float pendingImpulseStrength_ = 0.0f;
    float pendingImpulseRadius_ = 0.0f;
    bool pendingImpulseActive_ = false;
};
