#include "App.h"
#include "raymath.h"
#include "rlgl.h"
#include "DemoTrack.h"
#include "GlCompat.h"

#include "SpectrumRingVisualizer.h"
#include "GalaxyVisualizer.h"
#include "FireworksVisualizer.h"
#include "TunnelVisualizer.h"
#include "NeonFogVisualizer.h"

#include <filesystem>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
bool HasSupportedAudioExt(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac";
}

// Temporarily off: work is focused entirely on the Neon Fog particle/
// shape-morph pipeline (performance + legibility) for now, and audio
// playback was producing pops/clicks that were a distraction during that
// work, independent of whatever is causing them. Flip back on (and
// restore the other Add() calls below) once that's done. With this off,
// App::musicLoaded_ stays false, so Run() never calls UpdateMusicStream/
// AudioAnalyzer::Update -- NeonFogVisualizer's audio.* calls all read
// harmless zero-initialized defaults (see AudioAnalyzer.h), so lighting
// just stays at its resting color/intensity instead of erroring.
constexpr bool kAudioEnabled = false;
}

bool App::Init(const std::string& audioPathArg) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1920, 1080, "Particle Audio Visualizer");
    SetTargetFPS(60);

    // The requested 1920x1080 is a target aspect ratio, not a guarantee --
    // on a 1080p (or smaller) desktop that would exactly fill or exceed the
    // work area once the title bar/taskbar are accounted for. Clamp to 90%
    // of the current monitor's size (keeping the 16:9 aspect) and center
    // the window so the whole scene is always visible without the user
    // having to manually resize/reposition on first launch.
    {
        int monitor = GetCurrentMonitor();
        int maxW = static_cast<int>(GetMonitorWidth(monitor) * 0.9f);
        int maxH = static_cast<int>(GetMonitorHeight(monitor) * 0.9f);
        int w = 1920, h = 1080;
        if (w > maxW || h > maxH) {
            float scale = std::min(static_cast<float>(maxW) / w, static_cast<float>(maxH) / h);
            w = static_cast<int>(w * scale);
            h = static_cast<int>(h * scale);
        }
        SetWindowSize(w, h);
        SetWindowPosition((GetMonitorWidth(monitor) - w) / 2, (GetMonitorHeight(monitor) - h) / 2);
    }

    // This engine simulates and renders all particles on the GPU via
    // compute shaders and SSBOs; there is no CPU particle fallback. Fail
    // fast with a clear message rather than silently rendering nothing.
    if (!gl_compat::Init()) {
        CloseWindow();
        return false;
    }

    if (kAudioEnabled) InitAudioDevice();

    // ParticleRenderer's constructor issues real GL calls (compiles the
    // render shader, allocates a VAO), so it can only be constructed here,
    // after InitWindow()/gl_compat::Init() have confirmed a live GL 4.3
    // context exists -- see the comment on App::particleRenderer_.
    particleRenderer_ = std::make_unique<ParticleRenderer>(shaderLibrary_);
    postProcess_ = std::make_unique<PostProcess>(shaderLibrary_, GetScreenWidth(), GetScreenHeight());

    // Small soft-glow sprite for the few non-GPU-particle accent
    // billboards visualizers still draw directly (see RenderContext);
    // generated procedurally so the project ships with zero image assets.
    Image glow = GenImageGradientRadial(64, 64, 0.15f, WHITE, Color{ 255, 255, 255, 0 });
    accentTexture_ = LoadTextureFromImage(glow);
    UnloadImage(glow);

    if (kAudioEnabled) LoadAudio(audioPathArg);

    // Only Neon Fog while its particle/shape-morph pipeline is the sole
    // focus -- see kAudioEnabled. Restore these once that work lands.
    visualizers_.Add(std::make_unique<NeonFogVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<SpectrumRingVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<GalaxyVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<FireworksVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<TunnelVisualizer>(), shaderLibrary_, *particleRenderer_);

    camera_.up = { 0, 1, 0 };
    camera_.fovy = 45.0f;
    camera_.projection = CAMERA_PERSPECTIVE;
    camera_.target = { 0, 2.0f, 0 };
    camYaw_ = 0.0f;
    camPitch_ = 0.35f;
    camDistance_ = 20.0f;

    return true;
}

bool App::LoadAudio(const std::string& audioPathArg) {
    std::string pathToLoad;

    if (!audioPathArg.empty() && FileExists(audioPathArg.c_str())) {
        pathToLoad = audioPathArg;
    } else {
        fs::path audioDir = fs::path("assets") / "audio";
        if (fs::exists(audioDir) && fs::is_directory(audioDir)) {
            for (const auto& entry : fs::directory_iterator(audioDir)) {
                if (entry.is_regular_file() && HasSupportedAudioExt(entry.path())) {
                    pathToLoad = entry.path().string();
                    break;
                }
            }
        }
    }

    if (!pathToLoad.empty()) {
        music_ = LoadMusicStream(pathToLoad.c_str());
        trackLabel_ = fs::path(pathToLoad).filename().string();
        usingProceduralTrack_ = false;
    } else {
        proceduralTrackData_ = GenerateDemoTrackWav(90.0f, 44100);
        music_ = LoadMusicStreamFromMemory(".wav", proceduralTrackData_.data(),
                                            static_cast<int>(proceduralTrackData_.size()));
        trackLabel_ = "Procedural Demo Track (drop an mp3/wav/ogg into assets/audio to use your own)";
        usingProceduralTrack_ = true;
    }

    if (!IsMusicValid(music_)) {
        musicLoaded_ = false;
        return false;
    }

    music_.looping = true;
    SetMusicVolume(music_, 0.6f);
    PlayMusicStream(music_);
    analyzer_.AttachTo(music_);
    musicLoaded_ = true;
    return true;
}

void App::Run() {
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (musicLoaded_) UpdateMusicStream(music_);

        if (IsWindowResized()) {
            postProcess_->Resize(GetScreenWidth(), GetScreenHeight());
        }

        HandleInput(dt);
        UpdateCameraOrbit(dt);

        if (!paused_) {
            elapsedTime_ += dt;
            if (musicLoaded_) analyzer_.Update();
            FrameContext frame{ dt, elapsedTime_, analyzer_, intensity_ };
            visualizers_.Update(frame);
        }

        // Throttled GPU->CPU readback for the HUD particle counter --
        // see GpuParticleSystem::AliveCountApprox; querying every frame
        // would risk a pipeline stall for no visible benefit.
        hudReadbackTimer_ += dt;
        if (hudReadbackTimer_ >= 0.25f) {
            hudReadbackTimer_ = 0.0f;
            hudParticleCount_ = visualizers_.CurrentParticleCount();
        }

        Draw();
    }
}

void App::HandleInput(float dt) {
    if (IsKeyPressed(KEY_ONE)) visualizers_.SetIndex(0);
    if (IsKeyPressed(KEY_TWO)) visualizers_.SetIndex(1);
    if (IsKeyPressed(KEY_THREE)) visualizers_.SetIndex(2);
    if (IsKeyPressed(KEY_FOUR)) visualizers_.SetIndex(3);
    if (IsKeyPressed(KEY_FIVE)) visualizers_.SetIndex(4);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_TAB)) visualizers_.Next();
    if (IsKeyPressed(KEY_LEFT)) visualizers_.Prev();
    if (IsKeyPressed(KEY_S)) visualizers_.SecondaryActionOnCurrent();
    if (IsKeyPressed(KEY_M)) visualizers_.TertiaryActionOnCurrent();
    if (IsKeyDown(KEY_MINUS)) visualizers_.AdjustPrimaryOnCurrent(-dt * 0.6f);
    if (IsKeyDown(KEY_EQUAL)) visualizers_.AdjustPrimaryOnCurrent(dt * 0.6f);

    if (IsKeyPressed(KEY_SPACE) && musicLoaded_) {
        paused_ = !paused_;
        if (paused_) PauseMusicStream(music_);
        else ResumeMusicStream(music_);
    }

    if (IsKeyPressed(KEY_C)) autoRotate_ = !autoRotate_;
    if (IsKeyPressed(KEY_R)) {
        camYaw_ = 0.0f;
        camPitch_ = 0.35f;
        camDistance_ = 20.0f;
    }
    if (IsKeyPressed(KEY_F)) ToggleFullscreen();
    if (IsKeyPressed(KEY_H)) showHud_ = !showHud_;

    if (IsKeyDown(KEY_LEFT_BRACKET)) intensity_ -= dt * 0.6f;
    if (IsKeyDown(KEY_RIGHT_BRACKET)) intensity_ += dt * 0.6f;
    intensity_ = Clamp(intensity_, 0.25f, 3.0f);
}

void App::UpdateCameraOrbit(float dt) {
    if (autoRotate_) camYaw_ += dt * 0.15f;

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        camYaw_ -= d.x * 0.005f;
        camPitch_ = Clamp(camPitch_ - d.y * 0.005f, -1.4f, 1.4f);
    }

    camDistance_ -= GetMouseWheelMove() * 1.5f;
    camDistance_ = Clamp(camDistance_, 4.0f, 60.0f);

    camera_.position = {
        camDistance_ * cosf(camPitch_) * sinf(camYaw_),
        camDistance_ * sinf(camPitch_) + 3.0f,
        camDistance_ * cosf(camPitch_) * cosf(camYaw_)
    };
}

void App::Draw() {
    // The 3D scene renders into an offscreen target first (PostProcess::
    // BeginScene/EndScene) so bloom can extract and blur its bright areas
    // before the final composite goes to the backbuffer -- see
    // gfx/PostProcess.h.
    postProcess_->BeginScene();

    BeginMode3D(camera_);

    // The genuine view-projection matrix raylib just set up for this
    // frame's camera, reused by every GPU particle draw instead of each
    // visualizer re-deriving its own (and risking it drifting out of sync
    // with whatever BeginMode3D actually configured).
    Matrix viewProj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera_.target, camera_.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera_.up));
    Vector3 up = Vector3CrossProduct(right, forward);

    RenderContext ctx{ camera_, viewProj, right, up, accentTexture_ };

    // Blend mode is each visualizer's own responsibility now, not a
    // global wrap: additive suits self-luminous sparks/energy (bars,
    // fireworks, orbiting stars, lightning), but a dense fog cloud lit by
    // external lights needs standard alpha blending -- additive would
    // sum every overlapping sprite along a view ray without bound and
    // blow out to solid white well before it reads as "dense fog" (see
    // NeonFogVisualizer::Draw).
    visualizers_.Draw(ctx);

    EndMode3D();

    postProcess_->EndScene();

    BeginDrawing();
    ClearBackground(BLACK);
    postProcess_->Composite(bloomThreshold_, bloomIntensity_);

    if (showHud_) DrawHUD();

    EndDrawing();
}

void App::DrawHUD() const {
    const int pad = 12;
    int y = pad;

    DrawText(TextFormat("FPS: %d", GetFPS()), pad, y, 18, GREEN); y += 22;
    DrawText(TextFormat("Visualizer [%d/%d]: %s", visualizers_.CurrentIndex() + 1, visualizers_.Count(), visualizers_.CurrentName()),
              pad, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("Particles: %d", hudParticleCount_), pad, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("Intensity: %.2f", intensity_), pad, y, 18, RAYWHITE); y += 22;

    const char* extra = visualizers_.CurrentExtraStatusLine();
    if (extra != nullptr) { DrawText(extra, pad, y, 16, SKYBLUE); y += 20; }

    DrawText(TextFormat("Track: %s", trackLabel_.c_str()), pad, y, 16, GRAY); y += 20;
    if (paused_) { DrawText("PAUSED", pad, y, 18, YELLOW); y += 22; }

    const char* controls =
        "1-5: switch visualizer   Tab/Right/Left: cycle   Space: pause   S: cycle shape   M: toggle auto-cycle (Neon Fog)\n"
        "Right-drag: orbit camera   Wheel: zoom   C: toggle auto-rotate   R: reset camera\n"
        "[ / ]: light intensity   - / =: morph force (Neon Fog)   F: fullscreen   H: toggle HUD   Esc: quit";
    DrawText(controls, pad, GetScreenHeight() - 70, 16, Fade(RAYWHITE, 0.75f));
}

void App::Shutdown() {
    analyzer_.Detach();
    if (musicLoaded_) {
        StopMusicStream(music_);
        UnloadMusicStream(music_);
    }

    // Every GPU resource (particle SSBOs, compute programs, the render
    // VAO, cached shaders) must be released while the GL context is still
    // alive. App's own destructor runs *after* Shutdown() returns (it's a
    // stack-local in main(), destructed on scope exit), which would be
    // too late -- so everything GPU-owning is torn down explicitly here,
    // before CloseWindow(), rather than left to implicit destruction order.
    visualizers_ = VisualizerManager{};
    postProcess_.reset();
    particleRenderer_.reset();
    shaderLibrary_.UnloadAll();

    UnloadTexture(accentTexture_);
    CloseAudioDevice();
    CloseWindow();
}
