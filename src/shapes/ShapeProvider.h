#pragma once
#include "raylib.h"

class ShapeField;

// Fills a ShapeField's distance+gradient grid for the current frame. This
// is the extension point for driving fog structure from something other
// than a procedural analytic shape -- e.g. a future MeshShapeProvider
// that voxelizes a live face-tracked 3D model into the same field every
// frame, based on how close the face is being tracked, deforming as the
// tracked expression changes. Particles, forces, and shaders never see
// the provider; they only ever sample the ShapeField it filled, so
// swapping providers touches nothing else in the pipeline.
class IShapeProvider {
public:
    virtual ~IShapeProvider() = default;
    virtual void BakeInto(ShapeField& field, float time) = 0;

    // Optional debug-only wireframe of exactly what this provider baked,
    // for the Shape Bounds gizmo (see NeonFogVisualizer::Draw). Default
    // no-op, so a provider that hasn't implemented one yet just draws
    // nothing rather than needing an immediate override. Each provider
    // knows its own geometry exactly (an analytic formula today, a real
    // loaded Model's mesh for a future MeshShapeProvider) -- drawing it
    // directly here is always exact, unlike reconstructing an
    // approximation by resampling the baked field. `field` is provided
    // (not just a center) so an implementation can also cross-check
    // itself against the real baked data via field.SampleWorld() -- see
    // ProceduralShapeProvider's override. `offset` draws an approximate
    // SDF offset-surface instead of the exact one (0, the default) --
    // positive grows the shape outward, negative shrinks it inward -- by
    // adjusting each primitive's own characteristic radius/half-extent by
    // `offset`, which is exact for a sphere/box and a close approximation
    // for a torus/cylinder. Used by the Shell/Volume Bounds gizmo (see
    // NeonFogVisualizer::Draw) to visualize Shell Thickness (+/-offset)
    // and Volume Depth (-offset) without needing a second wireframe
    // implementation.
    virtual void DrawDebugWireframe(const ShapeField& /*field*/, Color /*color*/, float /*offset*/ = 0.0f) const {}
};
