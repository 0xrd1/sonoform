#include "ProceduralShapes.h"
#include "ShapeField.h"
#include "raymath.h"
#include <cmath>
#include <algorithm>

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

void ProceduralShapeProvider::DrawDebugWireframe(const ShapeField& field, Color color, float offset) const {
    Vector3 center = field.Center();
    // Self-check only makes sense against the exact surface (offset 0) --
    // an inflated/deflated wireframe is a deliberate approximation (see
    // ShapeProvider.h's comment), not a claim about the real baked field.
    bool selfCheck = (offset == 0.0f);
    switch (type_) {
        case ProceduralShapeType::Sphere: {
            float r = std::max(0.01f, kSphereRadius + offset);
            DrawSphereWires(center, r, 12, 12, color);
            if (selfCheck) CheckAgainstRealField(field, center, center + Vector3{ r, 0, 0 }, "Sphere");
            break;
        }
        case ProceduralShapeType::Box: {
            float h = std::max(0.01f, kBoxHalfExtent + offset);
            DrawCubeWiresV(center, Vector3{ h * 2.0f, h * 2.0f, h * 2.0f }, color);
            if (selfCheck) CheckAgainstRealField(field, center, center + Vector3{ h, 0, 0 }, "Box");
            break;
        }
        case ProceduralShapeType::Torus: {
            // Simplified to two flat horizontal rings at the outer/inner
            // major radius rather than a full tube wireframe (per-segment
            // tangent-frame math not worth it for a debug gizmo) -- still
            // confirms scale/position at a glance, which is the gizmo's job.
            // Offsetting a torus's SDF grows/shrinks its tube (minor)
            // radius while the major radius stays fixed -- see
            // ShapeProvider.h's comment.
            float minor = std::max(0.01f, (kTorusOuterRadius - kTorusInnerRadius) * 0.5f + offset);
            float major = (kTorusOuterRadius + kTorusInnerRadius) * 0.5f;
            float outer = major + minor;
            float inner = std::max(0.01f, major - minor);
            DrawCircle3D(center, outer, Vector3{ 1, 0, 0 }, 90.0f, color);
            DrawCircle3D(center, inner, Vector3{ 1, 0, 0 }, 90.0f, color);
            if (selfCheck) CheckAgainstRealField(field, center, center + Vector3{ outer, 0, 0 }, "Torus");
            break;
        }
        case ProceduralShapeType::Cylinder: {
            float r = std::max(0.01f, kCylinderRadius + offset);
            float h = std::max(0.01f, kCylinderHalfHeight + offset);
            DrawCylinderWires(center - Vector3{ 0, h, 0 }, r, r, h * 2.0f, 16, color);
            if (selfCheck) CheckAgainstRealField(field, center, center + Vector3{ r, 0, 0 }, "Cylinder");
            break;
        }
    }
}
