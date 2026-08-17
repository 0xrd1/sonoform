#pragma once
#include "raylib.h"
#include "LightSample.h"

class ShaderLibrary;
class ShapeField;

// A minimal stylized "stage" for a particle character to occupy: a dark
// floor plane with a soft glowing procedural grid (a depth/scale cue
// against an otherwise pure-black void, fading to fully transparent well
// before the plane's actual edge), a real contact shadow sampled from an
// actual top-down render of the particle mass (see ShadowMap below -- not
// the analytic shape silhouette, so it swirls/thins/shifts as the fog
// actually moves), and a real light contribution from the same lights[]
// array particles themselves are lit by (see Draw()'s lights/lightCount --
// mirrors particle_render.vert's uLightPositions/uLightColors/uLightCount
// and light-boost loop). See assets/shaders/env/void_floor.frag.
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
        Vector3 center{ 0, -6.0f, 0 };
        float size = 120.0f;
        // Was 45 -- at typical camera distance/pitch that read as a wide
        // lit plaza extending well past the frame, "open" rather than a
        // tight dark stage. Smaller now so the void closes in noticeably
        // closer around the fog -- see NeonFogVisualizer's verification
        // notes.
        float voidRadius = 14.0f;
        // Deliberately neutral -- the Tron-blue palette belongs to the
        // fog/lighting, not the stage (see NeonFogVisualizer's class
        // comment). Still clearly brighter than App's scene-clear color
        // (Color{6,6,12}, see PostProcess::BeginScene) so the floor reads
        // as present against the void instead of blending into it, but
        // darker than before (flat black/dark grey, not colored) so the
        // stage itself reads as a dim, close-in pool of light rather than
        // an evenly-lit room.
        Color baseColor{ 4, 4, 5, 255 };
        Color gridColor{ 20, 20, 22, 255 };
        float gridSpacing = 2.0f;
        float gridLineWidth = 0.09f;

        // Contact-shadow sampling (see ShapeField): world Y at which the
        // field is sampled -- normally the shape's own vertical center.
        float shapeSampleY = 2.0f;
        float shadowRadius = 3.5f;
        float shadowStrength = 0.8f;

        // Soft: this is a faint hint of where the (deliberately very
        // subtle, non-colored) key light falls, not a visible glow.
        Vector3 lightPoolCenter{ 0, -6.0f, 0 };
        float lightPoolRadius = 15.0f;
        float lightPoolStrength = 0.15f;
    };

    // A top-down render of the actual particle mass (see
    // NeonFogVisualizer::PreDraw), sampled by void_floor.frag in place of
    // the analytic-shape SDF shadow whenever `texture` is a valid
    // Texture2D (id != 0). `center`/`halfExtent` describe the world-space
    // XZ square the render covers (an orthographic camera looking straight
    // down), letting the floor shader map its own world position into the
    // texture's UV space.
    struct ShadowMap {
        Texture2D texture{};
        Vector2 center{ 0, 0 };
        float halfExtent = 1.0f;
    };

    // `shapeField` may be null -- the SDF-fallback shadow path is simply
    // skipped that frame (grid/vignette/light-pool still draw). `shadowMap`
    // (default-constructed, texture.id == 0) falls back to the SDF path
    // too -- see void_floor.frag's uHasShadowMap gate. `lights`/
    // `lightCount` mirror ParticleRenderer::Draw's identical parameters;
    // `lights` may be null (lightCount 0).
    void Draw(const Params& params, const ShapeField* shapeField,
              const ShadowMap& shadowMap = ShadowMap{},
              const LightSample* lights = nullptr, int lightCount = 0) const;

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

    int locHasShadowMap_ = -1;
    int locShadowMapTex_ = -1;
    int locShadowMapCenter_ = -1;
    int locShadowMapHalfExtent_ = -1;

    int locLightPositions_ = -1;
    int locLightColors_ = -1;
    int locLightCount_ = -1;

    // Lets Init() be called more than once on the same instance (the
    // runtime settings panel's "Rebuild Systems" action re-runs the owning
    // visualizer's whole Init()) without leaking the mesh VBO/VAO and
    // material each call would otherwise allocate fresh -- see Init()'s
    // definition.
    bool initialized_ = false;
};
