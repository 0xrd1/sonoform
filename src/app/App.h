#pragma once
#include <string>
#include <vector>
#include <memory>
#include "raylib.h"
#include "AudioAnalyzer.h"
#include "VisualizerManager.h"
#include "ShaderLibrary.h"
#include "ParticleRenderer.h"
#include "PostProcess.h"

class App {
public:
    // audioPathArg: optional path from the command line; empty means
    // "auto-detect" (scan assets/audio, else fall back to the procedural
    // demo track).
    bool Init(const std::string& audioPathArg);
    void Run();
    void Shutdown();

private:
    void HandleInput(float dt);
    void Update(float dt);
    void Draw();
    void DrawHUD() const;
    void UpdateCameraOrbit(float dt);
    bool LoadAudio(const std::string& audioPathArg);

    Camera3D camera_{};
    float camYaw_ = 0.0f;
    float camPitch_ = 0.35f;
    float camDistance_ = 20.0f;
    bool autoRotate_ = true;

    Music music_{};
    bool musicLoaded_ = false;
    bool usingProceduralTrack_ = false;
    std::vector<unsigned char> proceduralTrackData_; // must outlive `music_`
    std::string trackLabel_;

    AudioAnalyzer analyzer_;

    // GPU particle infrastructure, shared by every visualizer: ShaderLibrary
    // loads/caches compute + graphics shaders, ParticleRenderer owns the
    // one empty VAO + render program every GpuParticleSystem draws through.
    // ParticleRenderer's constructor issues real GL calls (compiles
    // shaders, allocates a VAO), so it cannot be a plain member with a
    // default initializer -- App's constructor runs in `main()` before
    // InitWindow(), when there is no GL context yet. It is constructed
    // inside Init() instead, once the context (and gl_compat::Init) is
    // confirmed ready. ShaderLibrary has no GL dependency at construction
    // (just an empty cache), so it's safe as a plain member.
    ShaderLibrary shaderLibrary_;
    std::unique_ptr<ParticleRenderer> particleRenderer_;
    std::unique_ptr<PostProcess> postProcess_; // same deferred-construction reasoning as particleRenderer_
    VisualizerManager visualizers_;

    // Small procedural soft-glow texture for the handful of non-GPU-particle
    // accent billboards a few visualizers still draw directly (see
    // RenderContext::accentTexture).
    Texture2D accentTexture_{};

    float intensity_ = 1.0f;
    float bloomThreshold_ = 0.35f;
    float bloomIntensity_ = 1.1f;
    bool paused_ = false;
    float elapsedTime_ = 0.0f;
    bool showHud_ = true;

    // Throttled HUD particle-count readback (see GpuParticleSystem::AliveCountApprox).
    int hudParticleCount_ = 0;
    float hudReadbackTimer_ = 0.0f;
};
