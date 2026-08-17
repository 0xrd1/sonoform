#include "VoidFloor.h"
#include "ShaderLibrary.h"
#include "ShapeField.h"
#include "raymath.h"
#include "rlgl.h"
#include <array>

void VoidFloor::Init(ShaderLibrary& shaders) {
    // Re-entrant: the settings panel's "Rebuild Systems" action re-runs the
    // owning visualizer's Init(), which calls this again on the same
    // instance. Release what a prior Init() allocated first, or the mesh
    // VBO/VAO and material leak on every rebuild. shader_ itself doesn't
    // need releasing here -- it's cached and owned by ShaderLibrary, so
    // LoadGraphics below just returns the same cached handle again.
    if (initialized_) {
        UnloadMesh(planeMesh_);
        UnloadMaterial(material_);
    }
    initialized_ = true;

    shader_ = shaders.LoadGraphics(
        "assets/shaders/env/void_floor.vert",
        "assets/shaders/env/void_floor.frag");

    // Unit quad in the XZ plane (GenMeshPlane's width/length are along X/Z,
    // matching DrawPlane's convention); scaled/translated per-draw via the
    // transform passed to DrawMesh -- see Draw().
    planeMesh_ = GenMeshPlane(1.0f, 1.0f, 1, 1);
    UploadMesh(&planeMesh_, false);

    material_ = LoadMaterialDefault();
    material_.shader = shader_;

    locFloorCenter_ = GetShaderLocation(shader_, "uFloorCenter");
    locVoidRadius_ = GetShaderLocation(shader_, "uVoidRadius");
    locBaseColor_ = GetShaderLocation(shader_, "uBaseColor");
    locGridColor_ = GetShaderLocation(shader_, "uGridColor");
    locGridSpacing_ = GetShaderLocation(shader_, "uGridSpacing");
    locGridLineWidth_ = GetShaderLocation(shader_, "uGridLineWidth");
    locHasShapeField_ = GetShaderLocation(shader_, "uHasShapeField");
    locShapeSampleY_ = GetShaderLocation(shader_, "uShapeSampleY");
    locShadowRadius_ = GetShaderLocation(shader_, "uShadowRadius");
    locShadowStrength_ = GetShaderLocation(shader_, "uShadowStrength");
    locLightPoolCenter_ = GetShaderLocation(shader_, "uLightPoolCenter");
    locLightPoolRadius_ = GetShaderLocation(shader_, "uLightPoolRadius");
    locLightPoolStrength_ = GetShaderLocation(shader_, "uLightPoolStrength");

    locHasShadowMap_ = GetShaderLocation(shader_, "uHasShadowMap");
    locShadowMapTex_ = GetShaderLocation(shader_, "uShadowMapTex");
    locShadowMapCenter_ = GetShaderLocation(shader_, "uShadowMapCenter");
    locShadowMapHalfExtent_ = GetShaderLocation(shader_, "uShadowMapHalfExtent");

    locLightPositions_ = GetShaderLocation(shader_, "uLightPositions");
    locLightColors_ = GetShaderLocation(shader_, "uLightColors");
    locLightCount_ = GetShaderLocation(shader_, "uLightCount");
}

VoidFloor::~VoidFloor() {
    // shader_ itself is owned by ShaderLibrary's cache and unloaded there
    // (see App::Shutdown) -- only the mesh/material this class allocated
    // directly are ours to release. UnloadMaterial is safe to call even
    // though material_ carries raylib's shared default texture map:
    // raylib guards against unloading the shared default texture id.
    UnloadMesh(planeMesh_);
    UnloadMaterial(material_);
}

namespace {
Vector3 ColorToVec3(Color c) {
    return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
}
}

void VoidFloor::Draw(const Params& p, const ShapeField* shapeField, const ShadowMap& shadowMap,
                      const LightSample* lights, int lightCount) const {
    rlEnableShader(shader_.id);

    // Binds the ShapeField's SSBO + grid-transform uniforms onto this now-
    // active shader, exactly the same rlEnableShader-then-bind pattern
    // GpuParticleSystem uses (see ShapeField::BindForSampling's comment).
    int hasShapeField = (shapeField != nullptr) ? 1 : 0;
    if (shapeField != nullptr) shapeField->BindForSampling(shader_.id);
    if (locHasShapeField_ != -1) SetShaderValue(shader_, locHasShapeField_, &hasShapeField, SHADER_UNIFORM_INT);

    Vector3 floorCenter = p.center;
    if (locFloorCenter_ != -1) SetShaderValue(shader_, locFloorCenter_, &floorCenter, SHADER_UNIFORM_VEC3);
    if (locVoidRadius_ != -1) SetShaderValue(shader_, locVoidRadius_, &p.voidRadius, SHADER_UNIFORM_FLOAT);

    Vector3 baseColor = ColorToVec3(p.baseColor);
    Vector3 gridColor = ColorToVec3(p.gridColor);
    if (locBaseColor_ != -1) SetShaderValue(shader_, locBaseColor_, &baseColor, SHADER_UNIFORM_VEC3);
    if (locGridColor_ != -1) SetShaderValue(shader_, locGridColor_, &gridColor, SHADER_UNIFORM_VEC3);
    if (locGridSpacing_ != -1) SetShaderValue(shader_, locGridSpacing_, &p.gridSpacing, SHADER_UNIFORM_FLOAT);
    if (locGridLineWidth_ != -1) SetShaderValue(shader_, locGridLineWidth_, &p.gridLineWidth, SHADER_UNIFORM_FLOAT);

    if (locShapeSampleY_ != -1) SetShaderValue(shader_, locShapeSampleY_, &p.shapeSampleY, SHADER_UNIFORM_FLOAT);
    if (locShadowRadius_ != -1) SetShaderValue(shader_, locShadowRadius_, &p.shadowRadius, SHADER_UNIFORM_FLOAT);
    if (locShadowStrength_ != -1) SetShaderValue(shader_, locShadowStrength_, &p.shadowStrength, SHADER_UNIFORM_FLOAT);

    Vector3 lightPoolCenter = p.lightPoolCenter;
    if (locLightPoolCenter_ != -1) SetShaderValue(shader_, locLightPoolCenter_, &lightPoolCenter, SHADER_UNIFORM_VEC3);
    if (locLightPoolRadius_ != -1) SetShaderValue(shader_, locLightPoolRadius_, &p.lightPoolRadius, SHADER_UNIFORM_FLOAT);
    if (locLightPoolStrength_ != -1) SetShaderValue(shader_, locLightPoolStrength_, &p.lightPoolStrength, SHADER_UNIFORM_FLOAT);

    // Real top-down particle shadow (see ShadowMap's comment) -- falls
    // back to the SDF-proximity path in void_floor.frag whenever no valid
    // texture is bound (id == 0), e.g. before NeonFogVisualizer::PreDraw
    // has ever run once.
    int hasShadowMap = (shadowMap.texture.id != 0) ? 1 : 0;
    if (locHasShadowMap_ != -1) SetShaderValue(shader_, locHasShadowMap_, &hasShadowMap, SHADER_UNIFORM_INT);
    if (hasShadowMap != 0) {
        if (locShadowMapTex_ != -1) SetShaderValueTexture(shader_, locShadowMapTex_, shadowMap.texture);
        if (locShadowMapCenter_ != -1) SetShaderValue(shader_, locShadowMapCenter_, &shadowMap.center, SHADER_UNIFORM_VEC2);
        if (locShadowMapHalfExtent_ != -1) SetShaderValue(shader_, locShadowMapHalfExtent_, &shadowMap.halfExtent, SHADER_UNIFORM_FLOAT);
    }

    // Same light array/falloff particles themselves are lit by -- see
    // particle_render.vert's identical uLightPositions/uLightColors/
    // uLightCount + light-boost loop, which this mirrors in
    // void_floor.frag. Always set uLightCount explicitly (never gated
    // behind "if lightCount > 0"): this shader program is the floor's
    // only user today, but the same "don't leak stale state across draw
    // calls on a shared program" reasoning ParticleRenderer::Draw
    // documents applies the moment that changes.
    int clampedCount = lightCount < 0 ? 0 : (lightCount > kMaxParticleLights ? kMaxParticleLights : lightCount);
    if (locLightCount_ != -1) SetShaderValue(shader_, locLightCount_, &clampedCount, SHADER_UNIFORM_INT);
    if (clampedCount > 0 && locLightPositions_ != -1 && locLightColors_ != -1) {
        std::array<Vector4, kMaxParticleLights> positions{};
        std::array<Vector4, kMaxParticleLights> colors{};
        for (int i = 0; i < clampedCount; i++) {
            const LightSample& l = lights[i];
            positions[static_cast<size_t>(i)] = { l.position.x, l.position.y, l.position.z, l.intensity };
            colors[static_cast<size_t>(i)] = { l.color.r / 255.0f, l.color.g / 255.0f, l.color.b / 255.0f, 0.0f };
        }
        SetShaderValueV(shader_, locLightPositions_, positions.data(), SHADER_UNIFORM_VEC4, clampedCount);
        SetShaderValueV(shader_, locLightColors_, colors.data(), SHADER_UNIFORM_VEC4, clampedCount);
    }

    Matrix transform = MatrixMultiply(MatrixScale(p.size, 1.0f, p.size),
                                       MatrixTranslate(p.center.x, p.center.y, p.center.z));
    DrawMesh(planeMesh_, material_, transform);
}
