#pragma once
#include "ShapeProvider.h"

// Must match SHAPE_SPHERE/SHAPE_BOX/SHAPE_TORUS/SHAPE_CYLINDER in
// shape_bake.comp. Ordered to make each step of the auto-cycle (see
// NeonFogVisualizer::CycleShapePreset) a visually distinct migration.
//
// There is no Head/face shape here -- an earlier crude analytic stand-in
// for a real face model was removed; the real thing will come later via a
// MeshShapeProvider (see shapes/ShapeProvider.h) driven by an actual 3D
// model / live face-tracking, not an approximate SDF.
enum class ProceduralShapeType {
    Sphere = 0,
    Box = 1,
    Torus = 2,
    Cylinder = 3,
};

// Bakes one of a small set of analytic SDFs into a ShapeField via the
// shared GPU bake compute shader (assets/shaders/shapes/shape_bake.comp).
// Proves the whole shape-field -> particle-conform pipeline end-to-end
// with zero external assets, and stands in for a future
// MeshShapeProvider driven by real face-tracking data. The shape is
// static per BakeInto call (the analytic SDFs don't animate), so callers
// only need to re-bake when the selected type changes -- see
// NeonFogVisualizer's shape-preset handling.
class ProceduralShapeProvider : public IShapeProvider {
public:
    explicit ProceduralShapeProvider(ProceduralShapeType type) : type_(type) {}

    void SetType(ProceduralShapeType type) { type_ = type; }
    ProceduralShapeType Type() const { return type_; }

    void BakeInto(ShapeField& field, float time) override;
    void DrawDebugWireframe(const ShapeField& field, Color color) const override;

private:
    ProceduralShapeType type_;
};
