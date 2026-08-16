#pragma once
#include "raylib.h"

class ShaderLibrary;
class ShapeField;

// A minimal stylized "stage" for a particle character to occupy: a dark
// floor plane with a soft glowing procedural grid (a depth/scale cue
// against an otherwise pure-black void, fading to fully transparent well
// before the plane's actual edge) and a soft contact shadow sampled
// directly from a ShapeField, so the shadow silhouette matches whatever
// the fog is currently attracted to. Deliberately not physically-based
// (no real shadow mapping) -- see assets/shaders/env/void_floor.frag.
//
// Drawn via GenMeshPlane + DrawMesh (a real VBO + raylib's auto-fed
// matModel/mvp uniforms) rather than the immediate-mode DrawPlane: the
// latter, combined with a custom shader here, was observed feeding
// corrupted per-vertex data into the fragment shader on this raylib/rlgl
// version (confirmed via direct varying dumps) even though it transformed
// geometry correctly -- DrawMesh is the robust, intended path for
// custom-shaded static geometry.
//
// Not fog-specific in implementation (it knows nothing about particles),
// even though NeonFogVisualizer is its only user for now.
class VoidFloor {
public:
    void Init(ShaderLibrary& shaders);
    ~VoidFloor();

    struct Params {
        Vector3 center{ 0, -3.5f, 0 };
        float size = 120.0f;
        // Large relative to the fog's own scale (shape radius ~2-3.5):
        // at typical camera distance/pitch the floor is seen at a shallow
        // angle, so a radius sized to just the fog's footprint would only
        // cover a few screen pixels directly behind it. This is tuned to
        // read as an actual ground plane extending well past the fog
        // before fading into the void, not a small patch hidden beneath
        // it -- see NeonFogVisualizer's verification notes.
        float voidRadius = 45.0f;
        // Clearly brighter than App's scene-clear color (Color{6,6,12},
        // see PostProcess::BeginScene) so the floor reads as present
        // against the void instead of blending into it.
        Color baseColor{ 14, 20, 40, 255 };
        Color gridColor{ 60, 170, 255, 255 };
        float gridSpacing = 2.0f;
        float gridLineWidth = 0.09f;

        // Contact-shadow sampling (see ShapeField): world Y at which the
        // field is sampled -- normally the shape's own vertical center.
        float shapeSampleY = 2.0f;
        float shadowRadius = 3.5f;
        float shadowStrength = 0.8f;

        Vector3 lightPoolCenter{ 0, -3.5f, 0 };
        float lightPoolRadius = 15.0f;
        float lightPoolStrength = 0.5f;
    };

    // `shapeField` may be null -- the contact shadow is simply skipped
    // that frame (grid/vignette/light-pool still draw).
    void Draw(const Params& params, const ShapeField* shapeField) const;

private:
    Shader shader_{};
    Mesh planeMesh_{};     // unit quad (see GenMeshPlane(1,1,1,1) in Init); scaled per-draw via the transform
    Material material_{};  // wraps shader_; owns no textures of its own

    int locFloorCenter_ = -1;
    int locVoidRadius_ = -1;
    int locBaseColor_ = -1;
    int locGridColor_ = -1;
    int locGridSpacing_ = -1;
    int locGridLineWidth_ = -1;
    int locHasShapeField_ = -1;
    int locShapeSampleY_ = -1;
    int locShadowRadius_ = -1;
    int locShadowStrength_ = -1;
    int locLightPoolCenter_ = -1;
    int locLightPoolRadius_ = -1;
    int locLightPoolStrength_ = -1;
};
