#pragma once
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

private:
    ShaderLibrary& shaders_;
    GpuBuffer buffer_;
    int resolution_;
    Vector3 center_;
    float halfExtent_;
    unsigned int bakeProgram_ = 0;
};
