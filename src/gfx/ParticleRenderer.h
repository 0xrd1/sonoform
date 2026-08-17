#pragma once
#include "raylib.h"
#include "LightSample.h"
#include "PaletteParams.h"

class ShaderLibrary;

// Shared instanced-billboard renderer used by every GpuParticleSystem. Owns
// one empty VAO -- no vertex buffer exists; particle_render.vert builds
// each quad corner purely from gl_VertexID and pulls particle data via
// gl_InstanceID from whichever buffer is bound at gpu_bindings::kParticleBuffer
// -- and the render shader program. Each particle system just binds its own
// buffer and calls Draw().
class ParticleRenderer {
public:
    explicit ParticleRenderer(ShaderLibrary& shaders);
    ~ParticleRenderer();

    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // Draws `instanceCount` particles from whichever particle buffer the
    // caller has already bound at gpu_bindings::kParticleBuffer.
    // fadeMode/sizeScale are forwarded straight to the vertex shader --
    // see particle_render.vert's FadeCurve and uSizeScale. `lights` may
    // be null (lightCount 0), in which case the shader's light uniforms
    // are still explicitly zeroed -- see particle_render.vert's comment
    // on why that matters for a shader program shared across visualizers.
    // `time` drives the slow shimmer in particle_render.frag's noise-mask
    // sprite breakup; defaults to 0.0 (a static, still-correct mask) for
    // callers that don't track elapsed time. `spriteStyle`: 0 = clean
    // circular sprite, 1 (default) = noise-broken wispy look -- see
    // particle_render.frag's uSpriteStyle.
    // `palette` defaults to PaletteMode::Off, an exact no-op against every
    // call site that doesn't pass one -- see gfx/PaletteParams.h.
    void Draw(int instanceCount, const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
              int fadeMode, float sizeScale, const LightSample* lights = nullptr, int lightCount = 0,
              float time = 0.0f, int spriteStyle = 1, const PaletteParams& palette = PaletteParams{}) const;

private:
    Shader shader_{};
    unsigned int vao_ = 0;

    int locViewProj_ = -1;
    int locCameraRight_ = -1;
    int locCameraUp_ = -1;
    int locFadeMode_ = -1;
    int locSizeScale_ = -1;
    int locLightPositions_ = -1;
    int locLightColors_ = -1;
    int locLightCount_ = -1;
    int locTime_ = -1;
    int locSpriteStyle_ = -1;

    int locPaletteMode_ = -1;
    int locPaletteCenter_ = -1;
    int locPaletteExtent_ = -1;
    int locPaletteHueA_ = -1;
    int locPaletteHueB_ = -1;
    int locPaletteSat_ = -1;
    int locPaletteStrength_ = -1;
    int locPaletteLightTint_ = -1;
};
