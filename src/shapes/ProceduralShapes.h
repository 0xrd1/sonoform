#pragma once
#include "ShapeProvider.h"

// Must match SHAPE_SPHERE/SHAPE_TORUS/SHAPE_HEAD in shape_bake.comp.
enum class ProceduralShapeType {
    Sphere = 0,
    Torus = 1,
    Head = 2,
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

private:
    ProceduralShapeType type_;
};
