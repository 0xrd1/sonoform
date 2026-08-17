#include "ParticleRenderer.h"
#include "ShaderLibrary.h"
#include "rlgl.h"
#include <array>

ParticleRenderer::ParticleRenderer(ShaderLibrary& shaders) {
    shader_ = shaders.LoadGraphics(
        "assets/shaders/particles/particle_render.vert",
        "assets/shaders/particles/particle_render.frag");

    locViewProj_ = GetShaderLocation(shader_, "uViewProj");
    locCameraRight_ = GetShaderLocation(shader_, "uCameraRight");
    locCameraUp_ = GetShaderLocation(shader_, "uCameraUp");
    locFadeMode_ = GetShaderLocation(shader_, "uFadeMode");
    locSizeScale_ = GetShaderLocation(shader_, "uSizeScale");
    locLightPositions_ = GetShaderLocation(shader_, "uLightPositions");
    locLightColors_ = GetShaderLocation(shader_, "uLightColors");
    locLightCount_ = GetShaderLocation(shader_, "uLightCount");
    locTime_ = GetShaderLocation(shader_, "uTime");
    locSpriteStyle_ = GetShaderLocation(shader_, "uSpriteStyle");

    // No vertex buffer is ever bound -- particle_render.vert generates
    // every quad corner from gl_VertexID -- but core-profile GL still
    // requires *a* VAO bound to issue any draw call, so we keep one
    // permanently empty VAO around for that purpose.
    vao_ = rlLoadVertexArray();
}

ParticleRenderer::~ParticleRenderer() {
    if (vao_ != 0) rlUnloadVertexArray(vao_);
    // shader_ is owned by ShaderLibrary's cache and unloaded there.
}

void ParticleRenderer::Draw(int instanceCount, const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
                             int fadeMode, float sizeScale, const LightSample* lights, int lightCount,
                             float time, int spriteStyle) const {
    if (instanceCount <= 0) return;

    rlEnableShader(shader_.id);

    if (locViewProj_ != -1) SetShaderValueMatrix(shader_, locViewProj_, viewProj);
    if (locCameraRight_ != -1) SetShaderValue(shader_, locCameraRight_, &cameraRight, SHADER_UNIFORM_VEC3);
    if (locCameraUp_ != -1) SetShaderValue(shader_, locCameraUp_, &cameraUp, SHADER_UNIFORM_VEC3);
    if (locFadeMode_ != -1) SetShaderValue(shader_, locFadeMode_, &fadeMode, SHADER_UNIFORM_INT);
    if (locSizeScale_ != -1) SetShaderValue(shader_, locSizeScale_, &sizeScale, SHADER_UNIFORM_FLOAT);
    if (locTime_ != -1) SetShaderValue(shader_, locTime_, &time, SHADER_UNIFORM_FLOAT);
    if (locSpriteStyle_ != -1) SetShaderValue(shader_, locSpriteStyle_, &spriteStyle, SHADER_UNIFORM_INT);

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

    // Depth *test* stays on (particles still hide behind solid geometry
    // like the Spectrum Ring's bars), but depth *write* must be off:
    // every particle is a full quad with a soft circular alpha falloff,
    // and if its mostly-transparent corners write depth anyway, they
    // punch a square-shaped hole through whatever should blend in behind
    // them -- the "black boxes around particles" artifact. Additive/
    // translucent sprites should never occlude each other via depth;
    // blending is what combines them correctly regardless of draw order.
    rlDisableDepthMask();
    rlEnableVertexArray(vao_);
    rlDrawVertexArrayInstanced(0, 6, instanceCount); // 6 verts = 2 tris = one camera-facing quad
    rlDisableVertexArray();
    rlEnableDepthMask();

    rlDisableShader();
}
