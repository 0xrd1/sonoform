#include "ProceduralShapes.h"
#include "ShapeField.h"
#include "raymath.h"
#include <cmath>

void ProceduralShapeProvider::BakeInto(ShapeField& field, float time) {
    (void)time; // analytic shapes here are static; a future animated/morphing provider would use it
    field.BakeProcedural(static_cast<int>(type_));
}

namespace {
// Must match shape_bake.comp's EvalShape literals exactly -- these are
// the *only* place in C++ that knows each primitive's true dimensions,
// used both for the debug wireframe below and its self-check. A rounded
// box's corner radius expands the surface *outward* on flat faces (it's
// not just a corner fillet inside the b-sized box) -- kBoxHalfExtent
// already includes that: shape_bake.comp bakes sdRoundBox(p, vec3(2.4),
// 0.4), whose flat-face zero-isosurface sits at 2.4+0.4=2.8, not 2.4
// (worked by hand: at p=(x,0,0), d(x) = x - 2.4 - 0.4). Getting this
// wrong once already (an earlier wireframe hardcoded 2.4) is exactly why
// the self-check below exists.
constexpr float kSphereRadius = 3.0f;
constexpr float kBoxHalfExtent = 2.8f; // = 2.4 (b) + 0.4 (corner radius)
constexpr float kTorusOuterRadius = 3.5f; // = 2.5 (major) + 1.0 (minor)
constexpr float kTorusInnerRadius = 1.5f; // = 2.5 (major) - 1.0 (minor)
constexpr float kCylinderRadius = 2.0f;
constexpr float kCylinderHalfHeight = 2.6f;

// One trilinear sample of the *real* baked field at each shape's own
// claimed surface point (along +X, on the shape's equatorial plane) --
// if it disagrees with 0 by more than this, the constants above have
// drifted from shape_bake.comp and something is now silently wrong
// again. Loose enough to absorb ordinary trilinear-interpolation error
// at typical grid resolutions, tight enough to catch a real mismatch
// (the box bug this guards against was off by 0.4).
constexpr float kSelfCheckTolerance = 0.15f;

void CheckAgainstRealField(const ShapeField& field, Vector3 center, Vector3 claimedSurfacePoint, const char* shapeName) {
    if (field.IsSampleCacheStale()) return; // caller refreshes the cache before calling; nothing to check yet if not
    float dist = field.SampleWorld(claimedSurfacePoint).distance;
    if (fabsf(dist) > kSelfCheckTolerance) {
        TraceLog(LOG_WARNING,
                 "ProceduralShapeProvider: %s debug wireframe disagrees with the real baked field by %.3f "
                 "world units at its claimed surface point -- the hardcoded dimensions in ProceduralShapes.cpp "
                 "no longer match shape_bake.comp's EvalShape.",
                 shapeName, dist);
    }
    (void)center;
}
} // namespace

void ProceduralShapeProvider::DrawDebugWireframe(const ShapeField& field, Color color) const {
    Vector3 center = field.Center();
    switch (type_) {
        case ProceduralShapeType::Sphere:
            DrawSphereWires(center, kSphereRadius, 12, 12, color);
            CheckAgainstRealField(field, center, center + Vector3{ kSphereRadius, 0, 0 }, "Sphere");
            break;
        case ProceduralShapeType::Box:
            DrawCubeWiresV(center, Vector3{ kBoxHalfExtent * 2.0f, kBoxHalfExtent * 2.0f, kBoxHalfExtent * 2.0f }, color);
            CheckAgainstRealField(field, center, center + Vector3{ kBoxHalfExtent, 0, 0 }, "Box");
            break;
        case ProceduralShapeType::Torus:
            // Simplified to two flat horizontal rings at the outer/inner
            // major radius rather than a full tube wireframe (per-segment
            // tangent-frame math not worth it for a debug gizmo) -- still
            // confirms scale/position at a glance, which is the gizmo's job.
            DrawCircle3D(center, kTorusOuterRadius, Vector3{ 1, 0, 0 }, 90.0f, color);
            DrawCircle3D(center, kTorusInnerRadius, Vector3{ 1, 0, 0 }, 90.0f, color);
            CheckAgainstRealField(field, center, center + Vector3{ kTorusOuterRadius, 0, 0 }, "Torus");
            break;
        case ProceduralShapeType::Cylinder:
            DrawCylinderWires(center - Vector3{ 0, kCylinderHalfHeight, 0 }, kCylinderRadius, kCylinderRadius,
                               kCylinderHalfHeight * 2.0f, 16, color);
            CheckAgainstRealField(field, center, center + Vector3{ kCylinderRadius, 0, 0 }, "Cylinder");
            break;
    }
}
