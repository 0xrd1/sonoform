#pragma once

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
};
