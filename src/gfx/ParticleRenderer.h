#pragma once
#include "raylib.h"
#include "LightSample.h"

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
    void Draw(int instanceCount, const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
              int fadeMode, float sizeScale, const LightSample* lights = nullptr, int lightCount = 0) const;

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
};
