#pragma once
#include <memory>
#include "raylib.h"
#include "GpuParticleSystem.h"
#include "EngineSettings.h"

class ShaderLibrary;
class ParticleRenderer;
class ShapeField;
namespace ui { class IParamVisitor; }

// The generic, reusable half of what NeonFogVisualizer used to do
// monolithically: a GPU particle population that spawns on/near a shared
// ShapeField's surface (GpuEmitMode::ShapeSurface) and is held there by a
// ShapeConform force. Owns exactly what's per-population -- the
// GpuParticleSystem, its four force indices, its spawn accumulator, and
// its own emission/force/attraction settings -- and nothing about the
// shared target shape itself (that's ShapeField, owned by the caller) or
// anything visualizer-level (lighting, floor, lightning, the morph driver).
// A second population attracted to the same shape (different color/size/
// behavior) is a second ShapeFogEmitter instance plus three more call
// sites (Init/Update/Draw) and one more VisitSettings group -- no
// duplicated logic.
class ShapeFogEmitter {
public:
    // Constructs the GpuParticleSystem (sized by emission.capacity) and
    // its four forces, and binds `field` for ShapeSurface-mode spawning
    // and ShapeConform sampling. Re-entrant, like GpuParticleSystem's own
    // construction pattern: safe to call again (e.g. from "Rebuild
    // Systems") to reallocate at a new capacity -- the prior
    // GpuParticleSystem is freed by the unique_ptr assignment before the
    // new one is constructed.
    void Init(ShaderLibrary& shaders, ParticleRenderer& renderer, ShapeField& field);

    // Advances forces/shading/spawn for one frame. `field` is the shared
    // target this emitter samples -- passed in rather than cached, so it
    // always reflects the caller's authoritative, possibly-just-rebaked
    // ShapeField (see ShapeField::SetCenter/SetHalfExtent). `morphStrength`
    // is the eased shape-attraction driver (owned by the caller). `shadeLightDir`/
    // `shadeAmbientFloor` feed SetShading every frame, so Lighting panel
    // edits apply live.
    void Update(float dt, float time, const ShapeField& field, float morphStrength,
                Vector3 shadeLightDir, float shadeAmbientFloor);

    // fadeMode is fixed at 2 (two-sided edge fade -- see
    // particle_render.vert's FadeCurve) and sizeScale at 1.0: neither has
    // needed to vary per-call across this project's history, so they're
    // not exposed here; add parameters if a future emitter needs them.
    void Draw(const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
              const LightSample* lights, int lightCount, float time, int spriteStyle) const;

    int AliveCountApprox() const { return system_ ? system_->AliveCountApprox() : 0; }

    // Wraps emission/force/attraction's own Visit() in named sub-groups --
    // see EngineSettings.h's FogEmissionSettings/FogForceSettings/
    // FogAttractionSettings. Caller wraps this in its own outer
    // BeginGroup/EndGroup (e.g. per-emitter name), same pattern as
    // NeonFogVisualizer::VisitSettings already uses for Lighting/Lightning/Floor.
    void VisitSettings(ui::IParamVisitor& v);

    ui::FogEmissionSettings emission;
    ui::FogForceSettings force;
    ui::FogAttractionSettings attraction;

private:
    std::unique_ptr<GpuParticleSystem> system_;

    int gravityForceIndex_ = -1;
    int turbulenceForceIndex_ = -1;
    int dragForceIndex_ = -1;
    int shapeConformForceIndex_ = -1;

    float spawnAccumulator_ = 0.0f;
};
