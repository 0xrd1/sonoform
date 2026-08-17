#pragma once
#include <vector>
#include "raylib.h"
#include "GpuBuffer.h"

class ShaderLibrary;

// A signed-distance + gradient field baked into a 3D grid, stored as an
// SSBO for GPU-side sampling (see assets/shaders/shapes/shape_field_sample.glsl).
// Represents an abstract "target shape" that particles can be attracted to
// and flow around via the ShapeConform force. Deliberately shape-source-
// agnostic: whatever fills the grid (a procedural SDF today, a voxelized
// face-tracked mesh later -- see ShapeProvider.h) is invisible to the
// particle/force code, which only ever samples the field this produces.
class ShapeField {
public:
    ShapeField(ShaderLibrary& shaders, int resolution, Vector3 center, float halfExtent);
    ~ShapeField();

    ShapeField(const ShapeField&) = delete;
    ShapeField& operator=(const ShapeField&) = delete;

    // Dispatches the shared analytic-shape bake compute shader
    // (shape_bake.comp), evaluating `shapeType` (see ProceduralShapeType)
    // at every grid voxel. Called by ProceduralShapeProvider::BakeInto;
    // a future MeshShapeProvider would instead use its own bake shader
    // but write into this same buffer layout.
    void BakeProcedural(int shapeType);

    // Binds the field's SSBO at gpu_bindings::kShapeFieldBuffer and sets
    // the grid-transform uniforms (uShapeGridDim/Center/HalfExtent) on
    // `program`. Called by GpuParticleSystem::Update for any system with
    // an associated ShapeField.
    void BindForSampling(unsigned int program) const;

    int Resolution() const { return resolution_; }
    Vector3 Center() const { return center_; }
    float HalfExtent() const { return halfExtent_; }

    // Plain field writes -- neither touches the SSBO, only `resolution_`
    // drives its allocation size (see the constructor). The caller is
    // responsible for calling BakeProcedural (or BakeInto on a
    // ShapeProvider) afterward so the baked field actually reflects the
    // new transform; these two are cheap enough to apply live, every
    // frame a setting changes, with no GPU reallocation.
    void SetCenter(Vector3 center) { center_ = center; }
    void SetHalfExtent(float halfExtent) { halfExtent_ = halfExtent; }

    // True whenever the baked SSBO has changed (via BakeProcedural) since
    // the last RefreshSampleCache() call -- lets callers avoid refreshing
    // (a real GPU->CPU readback, see RefreshSampleCache's comment) when
    // nothing actually needs a fresh CPU-side sample this frame.
    bool IsSampleCacheStale() const { return sampleCacheStale_; }

    // Populates a CPU-side mirror of the baked SSBO via a synchronous
    // readback (GpuBuffer::Read -- the same primitive
    // GpuParticleSystem::AliveCountApprox/DebugDumpFirst already use), so
    // SampleWorld() can be queried many times afterward with no further
    // GPU round-trips. A real cost, proportional to
    // Resolution()^3 * 16 bytes (~1MB at the default 40^3, ~14MB at the
    // max 96^3) -- callers (debug tooling; see NeonFogVisualizer::Draw)
    // must gate this to only the frames they're about to actually sample,
    // via IsSampleCacheStale() above, never call it unconditionally every
    // frame.
    void RefreshSampleCache();

    struct FieldSample {
        Vector3 gradient{ 0.0f, 1.0f, 0.0f };
        float distance = 0.0f;
    };

    // Trilinear sample of the cache populated by the last
    // RefreshSampleCache() call -- a byte-for-byte C++ port of
    // assets/shaders/shapes/shape_field_sample.glsl's SampleShapeField
    // (same world->grid transform, same 8-corner trilinear blend, same
    // clamp-to-edge boundary behavior), so results are identical to what
    // the GPU force actually samples. This is what debug gizmos (Shape
    // Bounds, Force Vectors -- see NeonFogVisualizer::Draw) sample instead
    // of re-deriving the shape's geometry by hand in C++, a pattern that
    // already caused one real, user-visible mismatch (a stale hardcoded
    // box half-extent) once the shader's SDF literals drifted from a
    // hand-maintained copy. Returns a harmless default (dist 0, gradient
    // +Y) if the cache has never been populated, rather than reading
    // uninitialized memory.
    FieldSample SampleWorld(Vector3 worldPos) const;

private:
    ShaderLibrary& shaders_;
    GpuBuffer buffer_;
    int resolution_;
    Vector3 center_;
    float halfExtent_;
    unsigned int bakeProgram_ = 0;

    std::vector<Vector4> cpuCache_; // mirrors the SSBO layout: xyz=gradient, w=distance
    bool sampleCacheStale_ = true;
};
